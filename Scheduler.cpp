//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
// EEco

#include "Scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <math.h>
#include <deque>

// Managing machines
// vector<vector<MachineId_t>> machine_matrix;

unsigned total_machines;



// // Current mips of vm
// unsigned vm_eff_mips(MachineId_t machine_id, VMId_t vm_id, Time_t curr) {
//     unsigned eff_mips = 0;

//     for (TaskId_t task_id : VM_GetInfo(vm_id).active_tasks) {
//         TaskInfo_t info = GetTaskInfo(task_id);

//         uint64_t remaining_instr = info.remaining_instructions;
//         uint64_t time_frame = info.target_completion - curr;

//         eff_mips += remaining_instr / time_frame;
//     }
    
// 	return eff_mips;
// }

double machineMaxMips(MachineWithVMs machine) {
    MachineInfo_t machineInfo = Machine_GetInfo(machine.machine_id);
    return (double) machineInfo.performance[machineInfo.p_state] * machineInfo.num_cpus;
}

// Current mips of machine
double machineCurMips(MachineWithVMs machine, Time_t now) {
    double mips = 0.0;

    for (VMId_t vmId : machine.vms) {
        for (TaskId_t taskId : VM_GetInfo(vmId).active_tasks) {
            TaskInfo_t task = GetTaskInfo(taskId);

            uint64_t remainingInstructions = task.remaining_instructions;
            uint64_t timeLeft = task.target_completion - now;

            // // somehow, remaining_instr is subject to unsigned integer overflow
            // if (remainingInstru > info.total_instructions) {
            //     continue;
            // }

            mips += (double) remainingInstructions / (double) timeLeft;
        }
    }

    return mips;
}

double getMachineCurUtil(MachineWithVMs machine, Time_t now) {
    double curMips = machineCurMips(machine, now);
    double maxMips = machineMaxMips(machine);

    return curMips / maxMips;
}

// Current mips of task
double getEstimatedTaskUtil(MachineWithVMs machine, TaskId_t task_id, Time_t curr) {
    TaskInfo_t task = GetTaskInfo(task_id);

    // // somehow, remaining_instr is subject to unsigned integer overflow
    // if (info.remaining_instructions > info.total_instructions) {
    //     return 0;
    // }

    uint64_t remainingInstructions = task.remaining_instructions;
    uint64_t timeLeft = task.target_completion - curr;

    double reqTaskMips = (double) remainingInstructions / (double) timeLeft;

    return reqTaskMips / machineMaxMips(machine);
}

// Converts SLA to Priority
Priority_t GetTaskPriorityFromSLA(TaskId_t task_id) {
    SLAType_t sla = GetTaskInfo(task_id).required_sla;

    switch (sla) {
      case SLA0:
        return HIGH_PRIORITY;
      case SLA1:
        return MID_PRIORITY;
      default:
        return LOW_PRIORITY;
    }
}





// unordered_map<unsigned, stateChangeInfo> idle_adjust_set;

// // For Idle set adjustment
// const uint64_t SECOND = 1000000;
// unsigned desired_idle_set_size;

// uint64_t last_time = 0;
// uint64_t wait_queue_time = 0;

// deque<unsigned> queue;



// Find a vm on the given machine for the task (or create one)
void AddTaskToMachine(MachineWithVMs* machine, TaskId_t task_id) {
    MachineInfo_t machineInfo = Machine_GetInfo(machine->machine_id);
    TaskInfo_t taskInfo = GetTaskInfo(task_id);

    for (VMId_t vm : machine->vms) {
        if (taskInfo.required_vm == VM_GetInfo(vm).vm_type) {
            VM_AddTask(vm, task_id, GetTaskPriorityFromSLA(taskInfo.required_sla));
            return;
        }
    }

    VMId_t newVM = VM_Create(taskInfo.required_vm, machineInfo.cpu);
    VM_Attach(newVM, machine.machine_id);
    VM_AddTask(newVM, task_id, GetTaskPriorityFromSLA(taskInfo.required_sla));
    machine.vms.push_back(newVM);

    return;
}

class MachineWithVMs {
public:
    MachineId_t machine_id;
    vector<VMId_t> vms;

    MachineWithVMs() = default;
    MachineWithVMs(MachineId_t id) : machine_id(id) {}
};

const float INIT_RUNNING = 0.5;
const float INIT_INTERMEDIATE = 0.2;
const float INIT_OFF = 0.3;

const MachineState_t RUNNING_STATE = S0;
const MachineState_t INTERMEDIATE_STATE = S2;
const MachineState_t OFF_STATE = S4;

vector<vector<MachineWithVMs*>> machinesByCPUType;

vector<vector<MachineWithVMs*>> running;
vector<vector<MachineWithVMs*>> intermediate;
vector<vector<MachineWithVMs*>> off;

unordered_map<MachineId_t, stateChangeInfo> curChangingState;

struct stateChangeInfo {
    MachineState_t oldState; // prev state of machine
    MachineState_t newState; // new state of machine
    TaskId_t* task; // if non-null, task to be assigned after state change
};

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    
    total_machines = Machine_GetTotal();

    // // we have four cpus
    // for(int i = 0 ; i < 4; i++) {
    //     vector<uint64_t> temp = {};
    //     machine_by_cpus.push_back(temp);
    //     vector<uint64_t> temp2 = {};
    //     running.push_back(temp2);
    //     vector<uint64_t> temp3 = {};
    //     idle.push_back(temp3);        
    //     vector<uint64_t> temp4 = {};
    //     off.push_back(temp4);
    // }
    
    for(int i = 0; i < total_machines; i++) {
        CPUType_t CPU = Machine_GetInfo((MachineId_t) i).cpu;

        MachineWithVMs* machine = new MachineWithVMs(i);
        machinesByCPUType[CPU].push_back(machine);

        // vector<MachineId_t> temp = {};
        // machine_matrix.push_back(temp);
    }

    for(int i = 0; i < machinesByCPUType.size(); i++) {
        int num_running = ceil((double) machinesByCPUType[i].size() * INIT_RUNNING);
        int num_intermediate = ceil((double) machinesByCPUType[i].size() * INIT_INTERMEDIATE);

        for(int j = 0; j < num_running; j++) {
            running[i].push_back(machinesByCPUType[i][j]);
        }

        for(int j = num_running; j < num_running + num_intermediate; j++) {
            Machine_SetState(machinesByCPUType[i][j]->machine_id, INTERMEDIATE_STATE);
            curChangingState[machinesByCPUType[i][j]->machine_id] = {RUNNING_STATE, INTERMEDIATE_STATE, nullptr};
        }
        for(int j = num_running + num_intermediate; j < machinesByCPUType[i].size(); j++) {
            Machine_SetState(machinesByCPUType[i][j]->machine_id, OFF_STATE);
            curChangingState[machinesByCPUType[i][j]->machine_id] = {RUNNING_STATE, OFF_STATE, nullptr};
        }
    }
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task = GetTaskInfo(task_id);

    for (MachineWithVMs* machine : running[task.required_cpu]) {
        if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
            ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
        }

        double machineCurUtil = getMachineCurUtil(*machine, now);
        double taskUtilEstimate = getEstimatedTaskUtil(*machine, task_id, now);
        bool canHandleTaskUtil = machineCurUtil + taskUtilEstimate <= 0.999;

        unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
        unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
        bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD;

        bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

        if (!currentlyChangingState && canHandleTaskUtil && canHandleTaskMem) {
            AddTaskToMachine(machine, task_id);
            return;
        }
    }

    for (MachineWithVMs* machine : intermediate[task.required_cpu]) {
        if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
            ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
        }

        unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
        unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
        bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD;

        bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

        if (!currentlyChangingState && canHandleTaskMem) {
            Machine_SetState(machine->machine_id, RUNNING_STATE);
            curChangingState[machine->machine_id] = {INTERMEDIATE_STATE, RUNNING_STATE, &task_id};
            return;
        }
    }

    for (MachineWithVMs* machine : off[task.required_cpu]) {
      if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
        ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
      }

      unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
      unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
      bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD;

      bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

      if (!currentlyChangingState && canHandleTaskMem) {
          Machine_SetState(machine->machine_id, RUNNING_STATE);
          curChangingState[machine->machine_id] = {OFF_STATE, RUNNING_STATE, &task_id};
          return;
      }
    }

    queue.push_back(task_id); // could not schedule the task now, put it in the waiting queue
}

void updateWaitingQueue(Time_t now) {
    //pop off tasks that are waiting
    while(queue.size() > 0) {
        TaskId_t task_id = queue[0];
        TaskInfo_t task = GetTaskInfo(task_id);
        CPUType_t task_cpu = task.required_cpu;
        bool done = false;
        for (MachineId_t id : running[task_cpu]) {
            MachineInfo_t machine = Machine_GetInfo(id);

            double machine_util = machine_eff_mips(id, now);
            double task_util = task_eff_mips(task_id, id, now);
            double machine_max_util = machine.performance[machine.p_state] * machine.num_cpus;

            bool correct_cpu = task.required_cpu == machine.cpu;
            bool enough_mem = machine.memory_used + task.required_memory + 8 < machine.memory_size;
            bool enough_util = machine_util + task_util <= machine_max_util;

            // dev notes: data struct to keep track of vm by vm_type?
            // dev notes: is the change_state.count required?
            if (!changing_state.count(id) && correct_cpu && enough_mem && enough_util) {
                FindVMAddTask(id, task_id);
                queue.pop_front();
                done = true;
                break;
            }
        }
        if(done) {
            continue;
        }

        for (MachineId_t id : idle[task_cpu]) {
            MachineInfo_t machine = Machine_GetInfo(id);

            bool correct_cpu = task.required_cpu == machine.cpu;
            bool enough_mem = machine.memory_used + task.required_memory + 8 < machine.memory_size;

            // dev notes: state change issues? (if idle is moving around)
            // dev notes: is the change_state.count required?
            if (!changing_state.count(id) && correct_cpu && enough_mem) {
                // dev notes: state change issues?
                Machine_SetState(id, RUNNING_S_STATE);
                changing_state[id] = {IDLE_S_STATE, RUNNING_S_STATE, true, task_id};
                queue.pop_front();
                done = true;
                break;
            }
        }
        if(done) {
            continue;
        }

        for (MachineId_t id : off[task_cpu]) {
            MachineInfo_t machine = Machine_GetInfo(id);

            bool correct_cpu = task.required_cpu == machine.cpu;
            bool enough_mem = machine.memory_used + task.required_memory + 8 < machine.memory_size;

            // dev notes: state change issues? (if idle is moving around)
            // dev notes: is the change_state.count required?
            if (!changing_state.count(id) && correct_cpu && enough_mem) {
                // dev notes: state change issues?
                Machine_SetState(id, RUNNING_S_STATE);
                changing_state[id] = {OFF_S_STATE, RUNNING_S_STATE, true, task_id};
                queue.pop_front();
                done = true;
                break;
            }
        }

        if(done) {
            continue;
        }
        break;
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
    // about 1 sec between checks
    
    if(now - wait_queue_time >= SECOND / 10) {
        wait_queue_time = now;
        updateWaitingQueue(now);
    }

    if(now - last_time >= SECOND) {
        last_time = now;
        for(int i = 0; i < 4; i++) {
            if(machine_by_cpus[i].size() != 0) {
                unsigned cpu_total_mips = 0;
                unsigned cpu_max_mips = 0;
                
                for (MachineId_t id : running[i]) {
                    MachineInfo_t info = Machine_GetInfo(id);
                    cpu_total_mips += machine_eff_mips(id, now);
                    cpu_max_mips += info.performance[info.p_state] * info.num_cpus;
                }
                
                double cpu_util = cpu_max_mips == 0 ? 0 : (double) cpu_total_mips / (double) cpu_max_mips;

                if (cpu_util > 0.7) {
                    double theoretical_util = cpu_util;
                    
                    unsigned idle_index = 0;
                    while (idle_index < idle[i].size() && theoretical_util > 0.7) {
                        MachineId_t id = idle[i][idle_index];
                        if(!changing_state.count(id)) {
                            Machine_SetState(id, RUNNING_S_STATE);
                            changing_state[id] = {IDLE_S_STATE, RUNNING_S_STATE, false, 0};
                            
                            MachineInfo_t info = Machine_GetInfo(id);
                            cpu_max_mips += info.performance[P0] * info.num_cpus;
                            theoretical_util = cpu_max_mips == 0 ? 0 : cpu_total_mips / cpu_max_mips;
                        }

                        idle_index++;
                    }
            
                    //idle change did not solve it
                    unsigned off_index = 0;
                    while (off_index < off[i].size() && theoretical_util > 0.7) {
                        MachineId_t id = off[i][off_index];
                        if(!changing_state.count(id)) {
                            Machine_SetState(id, RUNNING_S_STATE);
                            changing_state[id] = {OFF_S_STATE, RUNNING_S_STATE, false, 0};
                            
                            MachineInfo_t info = Machine_GetInfo(id);
                            cpu_max_mips += info.performance[P0] * info.num_cpus;
                            theoretical_util = cpu_max_mips == 0 ? 0 : cpu_total_mips / cpu_max_mips;
                        }

                        off_index++;
                    }

                    //check if idle size is too small
                    int idle_size = idle[i].size();
                    unsigned offset_index = 0;
                    while(offset_index < idle_size && idle_size < machine_by_cpus[i].size() * 0.2) {
                        MachineId_t id = off[i][offset_index];
                        if(!changing_state.count(id)) {
                            Machine_SetState(id, IDLE_S_STATE);
                            changing_state[id] = {OFF_S_STATE, IDLE_S_STATE, false, 0};
                            idle_size++;
                        }

                        offset_index++;
                    }

                } else if (cpu_util < 0.3) {

                    double theoretical_util = cpu_util;
                    
                    unsigned index = 0;
                    while (index < running[i].size() && theoretical_util < 0.3) {
                        MachineId_t id = running[i][index];
                        if(machine_eff_mips(id, now) == 0.0) {
                            if (!changing_state.count(id)) {
                                Machine_SetState(id, IDLE_S_STATE);
                                changing_state[id] = {RUNNING_S_STATE, IDLE_S_STATE, false, 0};

                                MachineInfo_t info = Machine_GetInfo(id);

                                if (info.performance[P0] * info.num_cpus > cpu_max_mips) {
                                    break;
                                }

                                cpu_max_mips -= info.performance[P0] * info.num_cpus;

                                theoretical_util = cpu_max_mips == 0 ? 0 : cpu_total_mips / cpu_max_mips;
                            }
                        }

                        index++;
                    }

                    //check if idle size is too big
                    int idle_size = idle[i].size();
                    unsigned idle_index = 0;
                    while(idle_index < idle_size && idle_size > (double) machine_by_cpus[i].size() * 0.5) {
                        MachineId_t id = idle[i][idle_index];
                        if(!changing_state.count(id)) {
                            Machine_SetState(id, OFF_S_STATE);
                            changing_state[id] = {IDLE_S_STATE, OFF_S_STATE, false, 0};
                            idle_size--;
                        }
                        idle_index++;
                    }
                }
            }
        }
    }
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
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
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
    stateChangeInfo info = curChangingState[machine_id];

    MachineWithVMs* machine;
    for (MachineWithVMs* m : machinesByCPUType[Machine_GetInfo(machine_id).cpu]) {
        if (m->machine_id == machine_id) {
            machine = m;
            break;
        }
    }

    if (Machine_GetInfo(machine_id).s_state != info.newState) {
        ThrowException("StateChangeComplete(): State change did not complete successfully for machine " + to_string(machine_id));
    }
    
    // either idle machine or off machine woken for task assignment
    if (info.newState == RUNNING_STATE && info.task != nullptr) {
        AddTaskToMachine(machine, *info.task);
    }

    CPUType_t machine_cpu = Machine_GetInfo(machine_id).cpu;
    // dev notes: state change issues?
    if (info.oldState == RUNNING_STATE) {
        running[machine_cpu].erase(remove(running[machine_cpu].begin(), running[machine_cpu].end(), machine_id), running[machine_cpu].end());
    } else if (info.old_state == IDLE_S_STATE) {
        idle[machine_cpu].erase(remove(idle[machine_cpu].begin(), idle[machine_cpu].end(), machine_id), idle[machine_cpu].end());
    } else {
        off[machine_cpu].erase(remove(off[machine_cpu].begin(), off[machine_cpu].end(), machine_id), off[machine_cpu].end());
    }

    // dev notes: state change issues?
    if (info.new_state == RUNNING_S_STATE) {
        running[machine_cpu].push_back(machine_id);
    } else if (info.old_state == IDLE_S_STATE) {
        idle[machine_cpu].push_back(machine_id);
    } else {
        off[machine_cpu].push_back(machine_id);
    }

    changing_state.erase(machine_id);
}