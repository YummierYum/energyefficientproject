#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <vector>
#include <unordered_map> // <-- FIX 1: Add this include

#include "Interfaces.h"

class Machine {
public:
    MachineId_t machine_id;
    std::vector<VMId_t> vms; // <-- Good practice to use std::vector

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
    std::vector<VMId_t> vms;
    std::vector<Machine*> x86_machines;
    std::vector<Machine*> arm_machines;
    std::vector<Machine*> power_machines;
    std::vector<Machine*> riscv_machines;
    std::unordered_map<TaskId_t, VMId_t> task_to_vm_map;
    std::unordered_map<VMId_t, Machine*> vm_to_machine_map;


    struct MachineLoad {
        Machine* machine;
        double utilization;

        MachineLoad(Machine* m, double u) : machine(m), utilization(u) {}

        bool operator<(const MachineLoad& other) const {
            return utilization < other.utilization;
        }
    };


    bool TryConsolidate(std::vector<Machine*>& machine_list, Time_t now);
};



#endif /* Scheduler_hpp */