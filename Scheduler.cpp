//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"

static bool migrating = false;
static unsigned active_machines = 0;

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    //
    int total_machines = Machine_GetTotal();
    active_machines = total_machines;

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 0);
    SimOutput("Scheduler::Init(): Initializing scheduler", 0);

    x86_machines.reserve(active_machines);
    for(unsigned i = 0; i < active_machines; i++) {
        SimOutput("<1>", 0);
        MachineId_t mid = MachineId_t(i);
        Machine_SetState(mid, S0);  //  simple always on policy, for benchmarking

        SimOutput("<2>", 0);

        CPUType_t cputype = Machine_GetCPUType(mid);
        VMId_t vmid = VM_Create(LINUX, cputype);
        VM_Attach(vmid, mid);

        SimOutput("<3>" + to_string(cputype), 0);

        Machine machine;
        machine.machine_id = mid;
        machine.vms.push_back(vmid);
        vms.push_back(vmid);

        SimOutput(to_string(machine.machine_id) +" " + to_string(machine.vms.size()), 0);

        SimOutput("<4>", 0);

        switch (cputype) {
        case X86:
            SimOutput("<5>", 0);
            SimOutput("x86 machines length before push: " + to_string(x86_machines.size()), 0);
            x86_machines.push_back(machine);
            SimOutput("<6>", 0);
            break;
        case ARM:
            arm_machines.push_back(machine);
            break;
        case POWER:
            power_machines.push_back(machine);
            break;
        case RISCV:
            riscv_machines.push_back(machine);
            break;
        default:
            break;
        }


    }

    SimOutput(to_string(active_machines) + " machines are selected for use by the scheduler", 0);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM, 
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired
    // Priority_t priority = (task_id == 0 || task_id == 64)? HIGH_PRIORITY : MID_PRIORITY;
    // if(migrating) {
    //     VM_AddTask(vms[0], task_id, priority);
    // }
    // else {
    //     VM_AddTask(vms[task_id % active_machines], task_id, priority);
    // }// Skeleton code, you need to change it according to your algorithm

    Priority_t priority = MID_PRIORITY;

    CPUType_t task_cpu = RequiredCPUType(task_id);
    vector<Machine>* possible_machines = nullptr;
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

    int least_memory_machine = -1;
    unsigned least_memory_used = UINT32_MAX;
    for (size_t i = 0; i < possible_machines->size(); i++) {
      unsigned memory_used = Machine_GetInfo((*possible_machines)[i].machine_id).memory_used;
      if (memory_used < least_memory_used) {
          least_memory_used = memory_used;
          least_memory_machine = i;
      }
    }

    vector<VMId_t>* possible_vms = &((*possible_machines)[least_memory_machine].vms);
    VMType_t task_vm = GetTaskInfo(task_id).required_vm;
    for (auto & vm : *possible_vms) {
        VMType_t current_vm_type = VM_GetInfo(vm).vm_type;
        if (current_vm_type == task_vm) {
            VM_AddTask(vm, task_id, priority);
            SimOutput("Scheduler::NewTask(): Assigned task " + to_string(task_id) 
                      + " to existing VM " + to_string(vm) 
                      + " on machine " 
                      + to_string((*possible_machines)[least_memory_machine].machine_id), 4);
            return;
        }
    }

    VMId_t new_vm = VM_Create(task_vm, task_cpu);
    MachineId_t target_machine = (*possible_machines)[least_memory_machine].machine_id;
    VM_Attach(new_vm, target_machine);
    (*possible_vms).push_back(new_vm);

    VM_AddTask(new_vm, task_id, priority);
    SimOutput("Scheduler::NewTask(): Created new VM " + to_string(new_vm)
              + " on machine " + to_string(target_machine)
              + " and assigned task " + to_string(task_id), 4);

    return;
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}

// Public interface below

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
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
    static unsigned counts = 0;
    counts++;
    if(counts == 10) {
        migrating = true;
        VM_Migrate(1, 9);
    }
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}

