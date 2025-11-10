//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <vector>

#include "Interfaces.h"

class Machine {
public:
    MachineId_t machine_id;
    vector<VMId_t> vms;

    Machine() = default;
    Machine(MachineId_t id) : machine_id(id) {}
};

class Scheduler {
public:
    Scheduler()                 {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
private:
    vector<VMId_t> vms;
    vector<Machine*> x86_machines;
    vector<Machine*> arm_machines;
    vector<Machine*> power_machines;
    vector<Machine*> riscv_machines;

    std::unordered_map<TaskId_t, VMId_t> task_to_vm_map;
    std::unordered_map<VMId_t, Machine*> vm_to_machine_map;

};



#endif /* Scheduler_hpp */