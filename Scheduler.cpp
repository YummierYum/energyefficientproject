//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include <string>     // For to_string
#include <iostream>   // For cout
#include <algorithm>  // For std::find
#include <vector>     // For vector
#include <unordered_map>

#include "Scheduler.hpp" // This file defines MachineId_t, VMId_t, etc.


using namespace std;
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
    active_machines = total_machines; // lets just use all machines for this simple scheduler

    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(total_machines), 1);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);

    for(unsigned i = 0; i < active_machines; i++) {
        MachineId_t mid = MachineId_t(i);
        Machine_SetState(mid, S0);  //  simple always on policy

        CPUType_t cputype = Machine_GetCPUType(mid);
        VMId_t vmid = VM_Create(LINUX, cputype);
        VM_Attach(vmid, mid); // start all machines with one VM

        Machine* machine = new Machine(mid);
        machine->vms.push_back(vmid); // track VMs on every machine
        vms.push_back(vmid); // track all VMs for shutting down later
        vm_to_machine_map[vmid] = machine;
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
    Priority_t priority = MID_PRIORITY; // all tasks have same priority in this simple scheduler

    // print task info for debugging
    // *** FIX: Commented out non-existent function 'GetTaskInfoString' ***
    // SimOutput(GetTaskInfoString(task_id), 1); 

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

    for (unsigned i = 0; i < possible_machines->size(); i++) {
       

        MachineInfo_t minfo = Machine_GetInfo((*possible_machines)[i]->machine_id);
        if (minfo.memory_used + VM_MEMORY_OVERHEAD + GetTaskMemory(task_id) <= minfo.memory_size) {
            VMId_t vmid = VM_Create(RequiredVMType(task_id), task_cpu);
            VM_Attach(vmid, (*possible_machines)[i]->machine_id);
            VM_AddTask(vmid, task_id, priority);
            possible_machines->at(i)->vms.push_back(vmid);
            vms.push_back(vmid);
            task_to_vm_map[task_id] = vmid;
            vm_to_machine_map[vmid] = possible_machines->at(i);
            return;
        }


    }
    // *** FIX: Removed 'Priority_t' to fix redeclaration error. This is now an assignment. ***
    priority = (task_id == 0 || task_id == 64)? HIGH_PRIORITY : MID_PRIORITY;
    if(migrating) {
        VM_AddTask(vms[0], task_id, priority);
    }
    else {
        VM_AddTask(vms[task_id % active_machines], task_id, priority);
    }// Skeleton code, you need to change it according to your algorithm
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
    auto task_map_entry = task_to_vm_map.find(task_id);
    if (task_map_entry == task_to_vm_map.end()) {
        SimOutput("Scheduler::TaskComplete(): WARNING - Task " + to_string(task_id) + " not in map! (Already completed?)", 1);
        return;
    }
    VMId_t vm_id = task_map_entry->second;
    task_to_vm_map.erase(task_map_entry);
    VMInfo_t vminfo = VM_GetInfo(vm_id);
    if (vminfo.active_tasks.empty()) { 
        
        SimOutput("Scheduler::TaskComplete(): VM " + to_string(vm_id) + " is now empty. Shutting down.", 4);
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
    // SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
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
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}