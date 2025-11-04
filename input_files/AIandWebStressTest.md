#test how the scheduler will marshall large ai tasks in the midst of constant low-intensity web and streaming tasks

machine class:
{
#beefy machines for gpu intensive tasks
        Number of machines: 16
        CPU type: X86
        Number of cores: 8
        Memory: 32768
        S-States: [200, 180, 140, 100, 80, 40, 0]
        P-States: [20,16,12,8]
        C-States: [20,5,2,0]
        MIPS: [1600, 1200, 1000, 500]
        GPUs: yes
}

machine class:
{
#light machines to handle frequent low-intensity requests
        Number of machines: 8
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12,8,6,4]
        C-States: [12,3,1,0]
        MIPS: [1000, 800, 600, 400]
        GPUs: no
}

task class:
{
#medium web task
        Start time: 0
        End time: 1800000000
        Inter arrival: 10000
        Expected runtime: 1800000000
        Memory: 8
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: ARM
        Task type: WEB
        Seed: 676767
}

task class:
{
#heavy web task - less frequent
        Start time: 0
        End time: 1800000000
        Inter arrival: 15000
        Expected runtime: 2000000
        Memory: 16
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: ARM
        Task type: WEB
        Seed: 676768
}

task class:
{
#light web task - more frequent
        Start time: 0
        End time: 1800000000
        Inter arrival: 5000
        Expected runtime: 1000000
        Memory: 4
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: ARM
        Task type: WEB
        Seed: 676769
}

task class:
{
#heavy ai task - occurs every minute
        Start time: 60000000
        End time: 1740000000
        Inter arrival: 60000000
        Expected runtime: 600000000
        Memory: 10240
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA2
        CPU type: X86
        Task type: AI 
        Seed: 676770
}

task class:
{
#frequent streaming tasks every .1 sec
        Start time: 0
        End time: 1800000000
        Inter arrival: 10000
        Expected runtime: 1000000
        Memory: 16
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: STREAM
        Seed: 676771
}