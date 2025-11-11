//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
// e-eco algorithm

#include "Scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <math.h>
#include <deque>

const Time_t SECOND = 1000000;
Time_t lastTaskWaitQueueCheck = 0;
Time_t lastPoolAdjustmentCheck = 0;

// HYPERPARAMETERS 
Time_t taskWaitQueueCheckInterval = SECOND / 10;
Time_t poolAdjustmentCheckInterval = SECOND;

const double UTIL_THRESHOLD_RUNNING = 0.7;
const double UTIL_THRESHOLD_IDLE = 0.3;

const float INIT_RUNNING = 0.5;
const float INIT_INTERMEDIATE = 0.3;
// const float INIT_OFF = 0.2;

const MachineState_t RUNNING_STATE = S0;
const MachineState_t INTERMEDIATE_STATE = S2;
const MachineState_t OFF_STATE = S3;

unsigned total_machines;

// track machines and their assigned VMs
class MachineWithVMs {
public:
    MachineId_t machine_id;
    vector<VMId_t> vms;

    MachineWithVMs() = default;
    MachineWithVMs(MachineId_t id) : machine_id(id) {}
};

// Maximum mips of machine
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
      case SLA2: 
        return MID_PRIORITY;
      default:
        return LOW_PRIORITY;
    }
}

// Find a vm on the given machine for the task (or create one)
void AddTaskToMachine(MachineWithVMs* machine, TaskId_t task_id) {
    SimOutput("AddTaskToMachine(): Adding task " + to_string(task_id) + " to machine " + to_string(machine->machine_id), 1);
    MachineInfo_t machineInfo = Machine_GetInfo(machine->machine_id);
    TaskInfo_t taskInfo = GetTaskInfo(task_id);

    for (VMId_t vm : machine->vms) {
        if (taskInfo.required_vm == VM_GetInfo(vm).vm_type) {
            VM_AddTask(vm, task_id, GetTaskPriorityFromSLA(taskInfo.required_sla));
            return;
        }
    }

    VMId_t newVM = VM_Create(taskInfo.required_vm, machineInfo.cpu);
    VM_Attach(newVM, machine->machine_id);
    VM_AddTask(newVM, task_id, GetTaskPriorityFromSLA(taskInfo.required_sla));
    machine->vms.push_back(newVM);

    return;
}

vector<vector<MachineWithVMs*>> machinesByCPUType; // size 4

vector<vector<MachineWithVMs*>> running; // size 4
vector<vector<MachineWithVMs*>> intermediate; // size 4
vector<vector<MachineWithVMs*>> off; // size 4

struct stateChange {
    MachineState_t oldState; // prev state of machine
    MachineState_t newState; // new state of machine
    int task; // task to be assigned after state change, -1 if no task
};

unordered_map<MachineId_t, stateChange> curChangingState;

deque<TaskId_t> tasksWaiting;

void Scheduler::Init() {    
    SimOutput("Begin initialization", 1);

    total_machines = Machine_GetTotal();
    machinesByCPUType.resize(4); // four cpu types: ARM, POWER, RISCV, X86
    running.resize(4);
    intermediate.resize(4);
    off.resize(4);
    for(int i = 0 ; i < 4; i++) {
        vector<MachineWithVMs*> cpu = {};
        machinesByCPUType[i] = cpu;
        
        vector<MachineWithVMs*> runList = {};
        running[i] = runList;

        vector<MachineWithVMs*> interList = {};
        intermediate[i] = interList;

        vector<MachineWithVMs*> offList = {};
        off[i] = offList;
    }
    
    for(unsigned i = 0; i < total_machines; i++) {
        CPUType_t CPU = Machine_GetInfo((MachineId_t) i).cpu;

        MachineWithVMs* machine = new MachineWithVMs(i);
        machinesByCPUType[CPU].push_back(machine);
    }

    for(unsigned i = 0; i < machinesByCPUType.size(); i++) {
        int num_running = ceil((double) machinesByCPUType[i].size() * INIT_RUNNING);
        int num_intermediate = ceil((double) machinesByCPUType[i].size() * INIT_INTERMEDIATE);

        for(int j = 0; j < num_running; j++) {
            running[i].push_back(machinesByCPUType[i][j]);
        }

        for(int j = num_running; j < num_running + num_intermediate; j++) {
            SimOutput("Scheduler::Init(): Setting machine " + to_string(machinesByCPUType[i][j]->machine_id) + " to INTERMEDIATE_STATE", 1);

            Machine_SetState(machinesByCPUType[i][j]->machine_id, INTERMEDIATE_STATE);
            curChangingState[machinesByCPUType[i][j]->machine_id] = {RUNNING_STATE, INTERMEDIATE_STATE, -1};
        }
        for(unsigned j = num_running + num_intermediate; j < machinesByCPUType[i].size(); j++) {
            SimOutput("Scheduler::Init(): Setting machine " + to_string(machinesByCPUType[i][j]->machine_id) + " to OFF_STATE", 1);

            Machine_SetState(machinesByCPUType[i][j]->machine_id, OFF_STATE);
            curChangingState[machinesByCPUType[i][j]->machine_id] = {RUNNING_STATE, OFF_STATE, -1};
        }
    }

    SimOutput("Finished initialization", 1);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // We are not migrating here. We transfer machines between running, intermediate, and off states via heuristics in the PeriodicCheck function.
}

// for some stats
int numTasksAssignedToIdleMachines = 0;
int numTasksAssignedToOffMachines = 0;

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::NewTask(): New task " + to_string(task_id) + " arrived at " + to_string(now), 1);

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
        bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD < machineMaxMem;

        bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

        if (!currentlyChangingState && canHandleTaskUtil && canHandleTaskMem) {
            SimOutput("Scheduler::NewTask(): Assigning task to already running machine.", 1);

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
        bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD < machineMaxMem;

        bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

        if (!currentlyChangingState && canHandleTaskMem) {
            SimOutput("Scheduler::NewTask(): Assigning task to intermediate machine.", 1);
            numTasksAssignedToIdleMachines++;
            Machine_SetState(machine->machine_id, RUNNING_STATE);
            curChangingState[machine->machine_id] = {INTERMEDIATE_STATE, RUNNING_STATE, int(task_id)};
            return;
        }
    }

    for (MachineWithVMs* machine : off[task.required_cpu]) {
      if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
        ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
      }

      unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
      unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
      bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD < machineMaxMem;

      bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

      if (!currentlyChangingState && canHandleTaskMem) {
          SimOutput("Scheduler::NewTask(): Assigning task to off machine.", 1);
          numTasksAssignedToOffMachines++;
          Machine_SetState(machine->machine_id, RUNNING_STATE);
          curChangingState[machine->machine_id] = {OFF_STATE, RUNNING_STATE, int(task_id)};
          return;
      }
    }

    SimOutput("Scheduler::NewTask(): No available machine for task " + to_string(task_id) + ". Adding to waiting queue.", 1);
    tasksWaiting.push_back(task_id); // task is waiting to run
}

void runWaitingTasks(Time_t now) {
    //pop off tasks that are waiting until we run into a task that can't be run yet
    int tasksPopped = 0;
    while(tasksWaiting.size() > 0 ) {
        tasksPopped++;
        SimOutput("Scheduler::runWaitingTasks(): Task queue not empty with size " + to_string(tasksWaiting.size()) + ". Attempting to pop ONE waiting task (id: " + to_string(tasksWaiting[0]) + " ) at " + to_string(now), 1);

        TaskId_t task_id = tasksWaiting[0];
        TaskInfo_t task = GetTaskInfo(task_id);

        if (task.completed) {
          ThrowException("runWaitingTasks(): Task " + to_string(task_id) + " is already completed but still in waiting queue");
        }

        CPUType_t task_cpu = task.required_cpu;
        bool taskAssigned = false;
        for (MachineWithVMs* machine : running[task_cpu]) {
            if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
                ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
            }

            double machineCurUtil = getMachineCurUtil(*machine, now);
            double taskUtilEstimate = getEstimatedTaskUtil(*machine, task_id, now);
            bool canHandleTaskUtil = machineCurUtil + taskUtilEstimate <= 0.999;

            unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
            unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
            bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD < machineMaxMem;
            
            bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;
            if (!currentlyChangingState && canHandleTaskUtil && canHandleTaskMem) {
                AddTaskToMachine(machine, task_id);
                tasksWaiting.pop_front();
                taskAssigned = true;
                break;
            }
        }
    
        if (taskAssigned) {
            continue;
        }

        for (MachineWithVMs* machine : intermediate[task_cpu]) {
            if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
                ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
            }

            unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
            unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
            bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD < machineMaxMem;

            bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;
            if (!currentlyChangingState && canHandleTaskMem) {
                Machine_SetState(machine->machine_id, RUNNING_STATE);
                curChangingState[machine->machine_id] = {INTERMEDIATE_STATE, RUNNING_STATE, int(task_id)};
                numTasksAssignedToIdleMachines++;
                tasksWaiting.pop_front();
                taskAssigned = true;
                break;
            }
        }

        if (taskAssigned) {
            continue;
        }

        for (MachineWithVMs* machine : off[task_cpu]) {
            if (task.required_cpu != Machine_GetInfo(machine->machine_id).cpu) {
                ThrowException("CPU type mismatch in NewTask"); // this shouldn't happen since we index into running by CPU type
            }

            unsigned machineMaxMem = Machine_GetInfo(machine->machine_id).memory_size;
            unsigned machineUsedMem = Machine_GetInfo(machine->machine_id).memory_used;
            bool canHandleTaskMem = machineUsedMem + task.required_memory + VM_MEMORY_OVERHEAD < machineMaxMem;

            bool currentlyChangingState = curChangingState.count(machine->machine_id) > 0;

            if (!currentlyChangingState && canHandleTaskMem) {
                unsigned taskId = task_id;
                Machine_SetState(machine->machine_id, RUNNING_STATE);
                curChangingState[machine->machine_id] = {OFF_STATE, RUNNING_STATE, int(taskId)};
                numTasksAssignedToOffMachines++;
                tasksWaiting.pop_front();
                taskAssigned = true;
                break;
            }
        }

        if (taskAssigned) {
            continue;
        }

        SimOutput("Scheduler::runWaitingTasks(): Could not find machine for waiting task " + to_string(task_id) + ". Leaving in queue.", 1);
        break;
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // transfer machines between running, intermediate, and off states via heuristics and total current utilization

    if(now - lastTaskWaitQueueCheck >= taskWaitQueueCheckInterval) {
        lastTaskWaitQueueCheck = now;
        runWaitingTasks(now);
    }

    if(now - lastPoolAdjustmentCheck >= poolAdjustmentCheckInterval) {
        SimOutput("Scheduler::PeriodicCheck(): Adjusting machine pools at " + to_string(now), 1);

        lastPoolAdjustmentCheck = now;
        for(int i = 0; i < 4; i++) { // cycle through every machine type
            if(machinesByCPUType[i].size() > 0) {
                double totalCompute = 0;
                double maxCompute = 0;
                
                for (MachineWithVMs* machine : running[i]) {
                    totalCompute += machineCurMips(*machine, now);
                    maxCompute += machineMaxMips(*machine);
                }

                double totalRunningUtil = totalCompute / maxCompute;

                if (totalRunningUtil > UTIL_THRESHOLD_RUNNING) {
                    unsigned intermediateIdx = 0;
                    while (intermediateIdx < intermediate[i].size() && totalRunningUtil > UTIL_THRESHOLD_RUNNING) {
                        MachineWithVMs* machine = intermediate[i][intermediateIdx];
                        if(!curChangingState.count(machine->machine_id)) {
                            Machine_SetState(machine->machine_id, RUNNING_STATE);
                            curChangingState[machine->machine_id] = {INTERMEDIATE_STATE, RUNNING_STATE, -1}; // ready up 

                            MachineInfo_t info = Machine_GetInfo(machine->machine_id);
                            maxCompute += machineMaxMips(*machine);
                            totalRunningUtil = totalCompute / maxCompute;
                        }

                        intermediateIdx++;
                    }
            
                    // Lets wake up some from the off state if we are still above threshold
                    unsigned offIdx = 0;
                    while (offIdx < off[i].size() && totalRunningUtil > UTIL_THRESHOLD_RUNNING) {
                        MachineWithVMs* machine = off[i][offIdx];
                        if(!curChangingState.count(machine->machine_id)) {
                            Machine_SetState(machine->machine_id, RUNNING_STATE);
                            curChangingState[machine->machine_id] = {OFF_STATE, RUNNING_STATE, -1};

                            MachineInfo_t info = Machine_GetInfo(machine->machine_id);
                            maxCompute += machineMaxMips(*machine);
                            totalRunningUtil = totalCompute / maxCompute;
                        }

                        offIdx++;
                    }

                    // migrate some from off to intermediate if below threshold
                    int j = 0;
                    int numOffMachines = off[i].size();
                    int numIntermediateMachines = intermediate[i].size();
                    int totalMachines = machinesByCPUType[i].size();
                    while(j < numOffMachines && numIntermediateMachines < (totalMachines * INIT_INTERMEDIATE)) {
                        MachineWithVMs *machine = off[i][j];
                        if(!curChangingState.count(machine->machine_id)) {
                          SimOutput("Scheduler::PeriodicCheck(): Moving machine " + to_string(off[i][j]->machine_id) + " from OFF_STATE to INTERMEDIATE_STATE", 1);
                            Machine_SetState(machine->machine_id, INTERMEDIATE_STATE);
                            curChangingState[machine->machine_id] = {OFF_STATE, INTERMEDIATE_STATE, -1};
                            numIntermediateMachines++;
                        }

                        j++;
                    }

                    return;
                }
                
                if (totalRunningUtil < UTIL_THRESHOLD_IDLE) {
                    
                    unsigned runningIdx = 0;
                    while (runningIdx < running[i].size() && totalRunningUtil < UTIL_THRESHOLD_IDLE) {
                        MachineWithVMs* machine = running[i][runningIdx];

                        bool canBeIdled = machineCurMips(*machine, now) == 0.0;
                        if(canBeIdled && !curChangingState.count(machine->machine_id)) {
                            Machine_SetState(machine->machine_id, INTERMEDIATE_STATE);
                            curChangingState[machine->machine_id] = {RUNNING_STATE, INTERMEDIATE_STATE, -1};

                            double lostMips = machineMaxMips(*machine);

                            if (lostMips < maxCompute) {
                                maxCompute -= lostMips;
                                totalRunningUtil = totalCompute / maxCompute;
                            } else {
                                break;
                            }
                        }

                        runningIdx++;
                    }

                    // migrate from intermediate to off
                    int numIntermediateMachines = intermediate[i].size();
                    int end = numIntermediateMachines;
                    int totalMachines = machinesByCPUType[i].size();
                    int j = 0;
                    while(j < end && numIntermediateMachines > (totalMachines * INIT_RUNNING)) {
                        MachineWithVMs *machine = intermediate[i][j];
                        bool canBeOffed = machineCurMips(*machine, now) == 0.0;
                        if(!curChangingState.count(machine->machine_id) && canBeOffed) {
                            SimOutput("Scheduler::PeriodicCheck(): Moving machine " + to_string(intermediate[i][j]->machine_id) + " from INTERMEDIATE_STATE to OFF_STATE", 1);
                            Machine_SetState(machine->machine_id, OFF_STATE);
                            curChangingState[machine->machine_id] = {INTERMEDIATE_STATE, OFF_STATE, -1};
                            numIntermediateMachines--;
                        }
                        j++;
                    }
                }
            }
        }
        SimOutput("Scheduler::PeriodicCheck(): Finished adjusting machine pools at " + to_string(now), 1);
    }
}

void Scheduler::Shutdown(Time_t time) {
    for(int i = 0; i < 4; i++) {
        for (MachineWithVMs* machine : machinesByCPUType[i]) {
          if(Machine_GetInfo(machine->machine_id).s_state != OFF_STATE && Machine_GetInfo(machine->machine_id).s_state != INTERMEDIATE_STATE) {
              for (VMId_t vm : machine->vms) {
                  VM_Shutdown(vm);
              }
              machine->vms.clear();
          } 
        }
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

    // runWaitingTasks(time); // we already probe the queue often enough for this not to really matter
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
    SimOutput("Total tasks completed: " + to_string(GetNumTasks()), 0);
    SimOutput("Num tasks assigned to idle machines: " + to_string(numTasksAssignedToIdleMachines), 0);
    SimOutput("Num tasks assigned to off machines: " + to_string(numTasksAssignedToOffMachines), 0);
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
    SimOutput("SLAWarning(): SLA violation warning for task " + to_string(task_id) + " at time " + to_string(time), 1);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    SimOutput("StateChangeComplete(): State change completed for machine " + to_string(machine_id) + " at time " + to_string(time) + "", 1);
    stateChange info = curChangingState[machine_id];

    MachineWithVMs* machine = nullptr;
    for (MachineWithVMs* m : machinesByCPUType[Machine_GetInfo(machine_id).cpu]) {
        if (m->machine_id == machine_id) {
            machine = m;
            break;
        }
    }

    if (machine == nullptr) {
        ThrowException("StateChangeComplete(): Could not find machine " + to_string(machine_id));
    }

    if (Machine_GetInfo(machine_id).s_state != info.newState) {
        ThrowException("StateChangeComplete(): State change did not complete successfully for machine " + to_string(machine_id));
    }
    
    // machine was woken up to run a task, so let's run that task
    if (info.newState == RUNNING_STATE && info.task != -1) {
        SimOutput("Adding a queued task " + to_string(info.task) + " to machine " + to_string(machine_id) + " after state change", 1);
        AddTaskToMachine(machine, info.task);
    }

    // remove machine from old state list
    CPUType_t cpu = Machine_GetInfo(machine_id).cpu;
    if (info.oldState == RUNNING_STATE) {
        running[cpu].erase(
            remove_if(
                running[cpu].begin(),
                running[cpu].end(),
                [machine_id](MachineWithVMs* machine) {
                    return machine->machine_id == machine_id;
                }
            ),
            running[cpu].end()
        );
    } else if (info.oldState == INTERMEDIATE_STATE) {
        intermediate[cpu].erase(
            remove_if(
                intermediate[cpu].begin(),
                intermediate[cpu].end(),
                [machine_id](MachineWithVMs* machine) {
                    return machine->machine_id == machine_id;
                }
            ),
            intermediate[cpu].end()
        );
    } else {
        off[cpu].erase(
            remove_if(
                off[cpu].begin(),
                off[cpu].end(),
                [machine_id](MachineWithVMs* machine) {
                    return machine->machine_id == machine_id;
                }
            ),
            off[cpu].end()
        );
    }

    // add machine to new state list
    if (info.newState == RUNNING_STATE) {
        running[cpu].push_back(machine);
    } else if (info.newState == INTERMEDIATE_STATE) {
        intermediate[cpu].push_back(machine);
    } else {
        off[cpu].push_back(machine);
    }

    curChangingState.erase(machine_id);
}