//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include <string>
#include <iostream>
#include <algorithm>  
#include <vector>
#include <limits>     
#include <deque>
#include "Scheduler.hpp"


using namespace std;
static bool migrating = false;
static unsigned active_machines = 0;


double find_energy_consumption(const MachineInfo_t& minfo) {
    double total_energy = 0;
    total_energy += minfo.num_cpus * (minfo.c_states[0] + minfo.p_states[0]) + minfo.s_states[0];
    return total_energy;
}


// Max MIPS of a machine
static double machineMaxMips(Machine* machine) {
    if (!machine) return 0.0;
    MachineInfo_t machineInfo = Machine_GetInfo(machine->machine_id);
    // Use the *current* p_state to get max MIPS in that state
    return (double) machineInfo.performance[machineInfo.p_state] * machineInfo.num_cpus;
}

// Current MIPS load of a machine
static double machineCurMips(Machine* machine, Time_t now) {
    if (!machine) return 0.0;
    double mips = 0.0;

    for (VMId_t vmId : machine->vms) {
        for (TaskId_t taskId : VM_GetInfo(vmId).active_tasks) {
            TaskInfo_t task = GetTaskInfo(taskId);
            // Avoid division by zero/negative if task is already late
            if (task.target_completion <= now) continue; 

            uint64_t remainingInstructions = task.remaining_instructions;
            // Ensure timeleft is at least 1 to prevent / 0
            uint64_t timeLeft = (task.target_completion > now) ? (task.target_completion - now) : 1;

            mips += (double) remainingInstructions / (double) timeLeft;
        }
    }
    return mips;
}

// MIPS load of a single VM
static double vmCurMips(VMId_t vm_id, Time_t now) {
    double mips = 0.0;
    for (TaskId_t taskId : VM_GetInfo(vm_id).active_tasks) {
        TaskInfo_t task = GetTaskInfo(taskId);
        if (task.target_completion <= now) continue; 

        uint64_t remainingInstructions = task.remaining_instructions;
        uint64_t timeLeft = (task.target_completion > now) ? (task.target_completion - now) : 1;

        mips += (double) remainingInstructions / (double) timeLeft;
    }
    return mips;
}


static double getMachineCurUtil(Machine* machine, Time_t now) {
    double curMips = machineCurMips(machine, now);
    double maxMips = machineMaxMips(machine);
    if (maxMips == 0) return 0.0;
    return curMips / maxMips;
}

static double getEstimatedTaskMips(TaskId_t task_id, Time_t curr) {
    TaskInfo_t task = GetTaskInfo(task_id);
    if (task.target_completion <= curr) return 0.0; // Task is already late

    uint64_t remainingInstructions = task.remaining_instructions;
    uint64_t timeLeft = (task.target_completion > curr) ? (task.target_completion - curr) : 1;

    return (double) remainingInstructions / (double) timeLeft;
}


void Scheduler::Init() {
    int total_machines = Machine_GetTotal();
    active_machines = total_machines; 

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 1);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    for(unsigned i = 0; i < active_machines; i++) {
        MachineId_t mid = MachineId_t(i);
        Machine_SetState(mid, S0); 

        CPUType_t cputype = Machine_GetCPUType(mid);
        VMId_t vmid = VM_Create(LINUX, cputype);
        VM_Attach(vmid, mid); 

        Machine* machine = new Machine(mid);
        machine->vms.push_back(vmid); 
        vms.push_back(vmid); 

        vm_to_machine_map[vmid] = machine;

        switch (cputype) {
        case X86: x86_machines.push_back(machine); break;
        case ARM: arm_machines.push_back(machine); break;
        case POWER: power_machines.push_back(machine); break;
        case RISCV: riscv_machines.push_back(machine); break;
        default: break;
        }
    }

  
    auto compare_by_energy = [](Machine* a, Machine* b) {
        return find_energy_consumption(Machine_GetInfo(a->machine_id)) < find_energy_consumption(Machine_GetInfo(b->machine_id));
    };

    sort(x86_machines.begin(), x86_machines.end(), compare_by_energy);
    sort(arm_machines.begin(), arm_machines.end(), compare_by_energy);
    sort(power_machines.begin(), power_machines.end(), compare_by_energy);
    sort(riscv_machines.begin(), riscv_machines.end(), compare_by_energy);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {

    
    if (!AttemptTaskPlacement(now, task_id)) {
        // If placement fails, add to the overflow queue instead of just printing an error
        SimOutput("Scheduler::NewTask: WARNING - Could not place task " + to_string(task_id) + ". No suitable VM or machine found. Adding to overflow queue.", 1);
        overflow_task_queue.push_back(task_id);
    }
}

void Scheduler::TryScheduleOverflowTasks(Time_t now) {
    // Keep processing the queue as long as it's not empty
    while (!overflow_task_queue.empty()) {
        
        TaskId_t task_id = overflow_task_queue.front(); // Look at the front task

        // Defensive check: In case task was completed by other means (unlikely in this model)
        TaskInfo_t task_info = GetTaskInfo(task_id);
        if (task_info.completed) {
            SimOutput("Scheduler::TryScheduleOverflowTasks: Task " + to_string(task_id) + " in queue is already complete. Removing.", 2);
            overflow_task_queue.pop_front();
            continue;
        }
        
        // Try to place the task from the front of the queue
        if (AttemptTaskPlacement(now, task_id)) {
            // Success!
            SimOutput("Scheduler::TryScheduleOverflowTasks: Successfully placed task " + to_string(task_id) + " from overflow queue.", 3);
            overflow_task_queue.pop_front(); // Remove it from the queue
            // Continue to the next task in the while loop
        } else {
            // Failure!
            // The system is still full. Stop trying to place tasks from the queue.
            // We'll try again later (on next TaskComplete or PeriodicCheck).
            SimOutput("Scheduler::TryScheduleOverflowTasks: Failed to place task " + to_string(task_id) + " from overflow queue. Stopping attempt.", 3);
            break; // Exit the while loop
        }
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    
    if (!overflow_task_queue.empty()) {
        SimOutput("Scheduler::PeriodicCheck: Attempting to schedule " + to_string(overflow_task_queue.size()) + " overflow tasks.", 3);
        TryScheduleOverflowTasks(now);
    }
    
}

void Scheduler::Shutdown(Time_t time) {
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}


bool Scheduler::TryConsolidate(std::vector<Machine*>& machine_list, Time_t now) {
    if (machine_list.size() < 2) {
        return false;
    }

    std::vector<Scheduler::MachineLoad> loads;
    for (Machine* m : machine_list) {
        double utilization = getMachineCurUtil(m, now);
        loads.emplace_back(m, utilization);
    }

    std::sort(loads.begin(), loads.end());

    Machine* least_machine = loads.front().machine;

    if (least_machine->vms.empty()) {
        return false;
    }

    VMId_t vm_to_migrate = (VMId_t)-1;
    double min_vm_mips = std::numeric_limits<double>::max();
    double vm_mem_footprint = 0;

    for (VMId_t vm_id : least_machine->vms) {
        VMInfo_t vminfo = VM_GetInfo(vm_id);
        if (vminfo.active_tasks.empty()) {
            continue; 
        }
        
        bool has_high_priority_task = false;
        double current_vm_mips = 0.0;
        double current_vm_mem = VM_MEMORY_OVERHEAD;

        for (TaskId_t tid : vminfo.active_tasks) {
            Priority_t task_prio =  (RequiredSLA(tid) == SLA0 || RequiredSLA(tid) == SLA1) ? HIGH_PRIORITY : MID_PRIORITY;
            if (task_prio == HIGH_PRIORITY) { 
                has_high_priority_task = true;
            }
            current_vm_mips += getEstimatedTaskMips(tid, now);
            current_vm_mem += GetTaskMemory(tid);
        }

        if (has_high_priority_task) {
            SimOutput("Scheduler::Consolidate: Skipping VM " + to_string(vm_id) + " (has HIGH_PRIORITY task).", 4);
            continue;
        }

        if (current_vm_mips < min_vm_mips) {
            min_vm_mips = current_vm_mips;
            vm_mem_footprint = current_vm_mem; // Store its memory footprint
            vm_to_migrate = vm_id;
        }
    }

    if (vm_to_migrate == (VMId_t)-1) {
        return false; // No suitable VM to migrate
    }

    const double HIGH_WATER_MARK = 0.90; // Don't pack a machine past 90% MIPS
    
    for (int i = loads.size() - 1; i >= 0; --i) {
        Machine* potential_target = loads[i].machine;
        
        if (potential_target == least_machine) {
            continue;
        }

        MachineInfo_t target_info = Machine_GetInfo(potential_target->machine_id);

       
        bool hasMemory = (target_info.memory_used + vm_mem_footprint <= target_info.memory_size);

        double maxMips = machineMaxMips(potential_target);
        double curMips = machineCurMips(potential_target, now);
        double newMipsUtil = (maxMips > 0) ? ((curMips + min_vm_mips) / maxMips) : 1.0;
        
        bool isNotOverloaded = (newMipsUtil < HIGH_WATER_MARK);

        if (hasMemory && isNotOverloaded)
        {
            // Found a suitable, safe target
            SimOutput("Scheduler::Consolidate: Migrating VM " + to_string(vm_to_migrate) +
                      " from machine " + to_string(least_machine->machine_id) +
                      " to machine " + to_string(potential_target->machine_id), 3);
            
            VM_Migrate(vm_to_migrate, potential_target->machine_id);
            migrating = true; 
            
            vm_to_machine_map[vm_to_migrate] = potential_target;

            auto& old_vms = least_machine->vms;
            // --- FIX: Use std::remove, not remove ---
            old_vms.erase(std::remove(old_vms.begin(), old_vms.end(), vm_to_migrate), old_vms.end());
            potential_target->vms.push_back(vm_to_migrate);
            
            return true; // Migration started, stop here
        }
    }
    // --- END FIX #4 ---

    return false; // No suitable target machine found
}

bool Scheduler::AttemptTaskPlacement(Time_t now, TaskId_t task_id) {
    
    Priority_t priority = (RequiredSLA(task_id) == SLA0 || RequiredSLA(task_id) == SLA1) ? HIGH_PRIORITY : MID_PRIORITY;
    
    CPUType_t task_cpu = RequiredCPUType(task_id);
    vector<Machine*>* possible_machines = nullptr;
    switch (task_cpu) {
    case X86:
        possible_machines = &x86_machines;
        break;
    case ARM:
        possible_machines = &arm_machines;
        break;
    case POWER:
        possible_machines = &power_machines;  
        break;
    case RISCV:
        possible_machines = &riscv_machines;
        break;
    default:
        break;
    }

    if (possible_machines) {
        // First pass: try to reuse an existing VM
        for (Machine* target_machine : *possible_machines) {
            for (VMId_t vm_id : target_machine->vms) {
                
                MachineInfo_t minfo = Machine_GetInfo(target_machine->machine_id);
                
                bool hasMemory = (minfo.memory_used + GetTaskMemory(task_id) <= minfo.memory_size);
                
                double maxMips = machineMaxMips(target_machine);
                double curMips = machineCurMips(target_machine, now);
                double taskMips = getEstimatedTaskMips(task_id, now);
                bool hasMips = (curMips + taskMips <= maxMips);

                if (hasMemory && hasMips) {
                    SimOutput("Scheduler::AttemptTaskPlacement: Reusing VM " + to_string(vm_id) + " for task " + to_string(task_id), 4);
                    VM_AddTask(vm_id, task_id, priority); 
                    task_to_vm_map[task_id] = vm_id;
                    return true; // <-- Success
                }
            }
        }

        // Second pass: try to create a new VM
        for (unsigned i = 0; i < possible_machines->size(); i++) {
            Machine* target_machine = possible_machines->at(i);
            MachineInfo_t minfo = Machine_GetInfo(target_machine->machine_id);

            bool hasMemory = (minfo.memory_used + VM_MEMORY_OVERHEAD + GetTaskMemory(task_id) <= minfo.memory_size);

            double maxMips = machineMaxMips(target_machine);
            double curMips = machineCurMips(target_machine, now);
            double taskMips = getEstimatedTaskMips(task_id, now);
            bool hasMips = (curMips + taskMips <= maxMips);

            if (hasMemory && hasMips) {
                VMId_t vmid = VM_Create(RequiredVMType(task_id), task_cpu);
                VM_Attach(vmid, target_machine->machine_id);
                VM_AddTask(vmid, task_id, priority); 
                
                target_machine->vms.push_back(vmid);
                vms.push_back(vmid);

                task_to_vm_map[task_id] = vmid;
                vm_to_machine_map[vmid] = target_machine;
                return true; // <-- Success
            }
        }
    }

    // All placement attempts failed
    return false; // <-- Failure
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);

    // ... (all your existing logic for finding the task and VM) ...
    auto task_map_entry = task_to_vm_map.find(task_id);
    if (task_map_entry == task_to_vm_map.end()) {
        // ... (your warning message) ...
        return;
    }
    
    VMId_t vm_id = task_map_entry->second;
    task_to_vm_map.erase(task_map_entry);
    VMInfo_t vminfo = VM_GetInfo(vm_id);

    // ... (all your existing logic for shutting down the VM if empty) ...
    if (vminfo.active_tasks.empty()) { 
       auto machine_map_entry = vm_to_machine_map.find(vm_id);

if (machine_map_entry != vm_to_machine_map.end()) {


Machine* machine = machine_map_entry->second;

auto& vms_on_machine = machine->vms;


auto vm_it = std::find(vms_on_machine.begin(), vms_on_machine.end(), vm_id);

if (vm_it != vms_on_machine.end()) {

vms_on_machine.erase(vm_it);

}



VM_Shutdown(vm_id);

vm_to_machine_map.erase(machine_map_entry);



vms.erase(std::remove(vms.begin(), vms.end(), vm_id), vms.end());

}
    }


    // --- ADD THIS SECTION ---
    // A task just completed, freeing resources.
    // Try to schedule any tasks waiting in the overflow queue.
    // if (!overflow_task_queue.empty()) {
    //     SimOutput("Scheduler::TaskComplete: Task finished. Checking overflow queue.", 4);
    //     TryScheduleOverflowTasks(now);
    // }
    // --- END ADDITION ---


    if (!migrating) {
        if (TryConsolidate(x86_machines, now)) return;
        if (TryConsolidate(arm_machines, now)) return;
        if (TryConsolidate(power_machines, now)) return;
        if (TryConsolidate(riscv_machines, now)) return;
    }
}

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}

void SchedulerCheck(Time_t time) {
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
}