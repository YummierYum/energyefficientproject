//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//  Surain Saigal & Tyler Kubecka

#include "Scheduler.hpp"

// simples string utils for debugging purposes (maybe we put these in a dedicated file later)
string GetVMInfoString(VMId_t vm_id) {
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    string info = "VM ID: " + to_string(vm_info.vm_id)
        + ", Machine ID: " + to_string(vm_info.machine_id)
        + ", CPU Type: " + to_string(vm_info.cpu)
        + ", VM Type: " + to_string(vm_info.vm_type)
        + ", Active Tasks: ";
    for (auto& task : vm_info.active_tasks) {
        info += to_string(task) + " ";
    }
    return info;
}

string GetTaskInfoString(TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    string info = "\nTask ID: " + to_string(task_info.task_id)
        + ", Required CPU: " + to_string(task_info.required_cpu)
        + ", Required VM: " + to_string(task_info.required_vm)
        + ", Required Memory: " + to_string(task_info.required_memory)
        + ", Priority: " + to_string(task_info.priority)
        + ", Completed: " + (task_info.completed ? "Yes" : "No");
    return info;
}

static bool migrating = false;
static unsigned active_machines = 0;

void Scheduler::Init() {
    int total_machines = Machine_GetTotal();
    active_machines = total_machines; // lets just use all machines for this simple scheduler

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 1);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    for (unsigned i = 0; i < active_machines; i++) {
        MachineId_t mid = MachineId_t(i);
        Machine_SetState(mid, S0);  //  simple always on policy

        CPUType_t cputype = Machine_GetCPUType(mid);
        VMId_t vmid = VM_Create(LINUX, cputype);
        VM_Attach(vmid, mid); // start all machines with one VM

        Machine* machine = new Machine(mid);
        machine->vms.push_back(vmid); // track VMs on every machine
        vms.push_back(vmid); // track all VMs for shutting down later

        switch (cputype) {
        case X86:
            x86_machines.push_back(machine);
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

    SimOutput(to_string(active_machines) + " out of " + to_string(total_machines) + " machines are selected for use by the scheduler", 1);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    Priority_t priority = MID_PRIORITY; // all tasks have same priority in this simple scheduler

    // print task info for debugging
    SimOutput(GetTaskInfoString(task_id), 1);

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

    int least_memory_machine = -1;
    unsigned least_memory_used = UINT32_MAX;
    for (size_t i = 0; i < possible_machines->size(); i++) {
        unsigned memory_used = Machine_GetInfo((*possible_machines)[i]->machine_id).memory_used;
        if (memory_used < least_memory_used) {
            least_memory_used = memory_used;
            least_memory_machine = i;
        }
    }

    SimOutput("Least memory machine id: " + to_string((*possible_machines)[least_memory_machine]->machine_id)
        + " with memory used: " + to_string(least_memory_used), 1);

    vector<VMId_t>* possible_vms = &((*possible_machines)[least_memory_machine]->vms);
    VMType_t task_vm = GetTaskInfo(task_id).required_vm;
    for (auto& vm : *possible_vms) {
        VMType_t current_vm_type = VM_GetInfo(vm).vm_type;
        if (current_vm_type == task_vm) {
            SimOutput("Chosen for VM: " + GetVMInfoString(vm), 1);
            VM_AddTask(vm, task_id, priority);
            SimOutput("Scheduler::NewTask(): Assigned task " + to_string(task_id)
                + " to existing VM " + to_string(vm)
                + " on machine "
                + to_string((*possible_machines)[least_memory_machine]->machine_id), 1);
            return;
        }
    }

    VMId_t new_vm = VM_Create(task_vm, task_cpu);
    MachineId_t target_machine = (*possible_machines)[least_memory_machine]->machine_id;
    VM_Attach(new_vm, target_machine);
    (*possible_vms).push_back(new_vm);

    VM_AddTask(new_vm, task_id, priority);
    SimOutput("Scheduler::NewTask(): Created new VM " + to_string(new_vm)
        + " on machine " + to_string(target_machine)
        + " and assigned task " + to_string(task_id), 1);

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
    for (auto& vm : vms) {
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
    // Scheduler.PeriodicCheck(time);
    // static unsigned counts = 0;
    // counts++;
    // if(counts == 10) {
    //     migrating = true;
    //     VM_Migrate(1, 9);
    // }
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time) / 1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}
