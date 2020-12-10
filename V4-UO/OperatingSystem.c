#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PrepareDaemons();
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int, char *);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_PrintQueue(int);
void OperatingSystem_HandleClockInterrupt();
int OperatingSystem_ExtractFromBlockedToReady();
void OperatingSystem_ChangeProcess(int);
void OperatingSystem_MoveToTheBLOCKEDState(int PID);
int OperatingSystem_isImportant(int, int);

void OperatingSystem_ReleaseMainMemory();
void OperatingSystem_AssignPartition(int, int, char *);
int OperatingSystem_CheckPartitions(int);
// In OperatingSystem.c Exercise 5-b of V2
// Heap with blocked processes sorted by when to wakeup
heapItem sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses = 0;
int numberOfClockInterrupts = 0;
// Array that contains the identifiers of the READY processes
heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];

int numberOfReadyToRunProcesses[NUMBEROFQUEUES] = {0, 0};

char *queueNames[NUMBEROFQUEUES] = {"USER", "DAEMONS"};

char *statesNames[5] = {"NEW", "READY", "EXECUTING", "BLOCKED", "EXIT"};
// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID = NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
int initialPID = PROCESSTABLEMAXSIZE - 1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList;

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses = 0;
// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex)
{
	OperatingSystem_InitializePartitionTable();
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// Obtain the memory requirements of the program
	int processSize = OperatingSystem_ObtainProgramSize(&programFile, "OperatingSystemCode");

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);

	// Process table initialization (all entries are free)
	for (i = 0; i < PROCESSTABLEMAXSIZE; i++)
	{
		processTable[i].busy = 0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base + 2);

	// Include in program list  all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);

	ComputerSystem_FillInArrivalTimeQueue();

	OperatingSystem_PrintStatus();

	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	if (numberOfNotTerminatedUserProcesses <= 0 && numberOfProgramsInArrivalTimeQueue <= 0)
	{
		OperatingSystem_ReadyToShutdown();
	}

	if (strcmp(programList[processTable[sipID].programListIndex]->executableName, "SystemIdleProcess"))
	{
		// Show red message "FATAL ERROR: Missing SIP program!\n"

		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99, SHUTDOWN, "FATAL ERROR: Missing SIP program!\n");
		exit(1);
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess = OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
void OperatingSystem_PrepareDaemons(int programListDaemonsBase)
{

	// Include a entry for SystemIdleProcess at 0 position
	programList[0] = (PROGRAMS_DATA *)malloc(sizeof(PROGRAMS_DATA));

	programList[0]->executableName = "SystemIdleProcess";
	programList[0]->arrivalTime = 0;
	programList[0]->type = DAEMONPROGRAM; // daemon program

	sipID = initialPID % PROCESSTABLEMAXSIZE; // first PID for sipID

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	baseDaemonsInProgramList = programListDaemonsBase;
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the
// 			command lineand daemons programs
int OperatingSystem_LongTermScheduler()
{

	int PID, i, numberOfSuccessfullyCreatedProcesses = 0;

	while (OperatingSystem_IsThereANewProgram() == 1)
	{
		i = Heap_poll(arrivalTimeQueue, QUEUE_ARRIVAL, &numberOfProgramsInArrivalTimeQueue);
		PID = OperatingSystem_CreateProcess(i);

		switch (PID)
		{
		case NOFREEENTRY:
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(103, ERROR, programList[i]->executableName);
			continue;

		case PROGRAMDOESNOTEXIST:
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104, ERROR, programList[i]->executableName, "-- it does not exist --");
			continue;

		case PROGRAMNOTVALID:
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104, ERROR, programList[i]->executableName, "-- invalid priority or size --");
			continue;

		case TOOBIGPROCESS:
			OperatingSystem_ShowTime(SYSMEM);
			ComputerSystem_DebugMessage(105, ERROR, programList[i]->executableName);
			continue;

		case MEMORYFULL:
		{
			OperatingSystem_ShowTime(SYSMEM);
			ComputerSystem_DebugMessage(144, ERROR, programList[i]->executableName);
			continue;
		}
		default:
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type == USERPROGRAM)
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
			continue;
		}
	}
	if (numberOfSuccessfullyCreatedProcesses > 0)
	{
		OperatingSystem_PrintStatus();
	}
	else if (numberOfNotTerminatedUserProcesses == 0 && numberOfProgramsInArrivalTimeQueue == 0)
	{
		OperatingSystem_ReadyToShutdown();
	}
	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram)
{
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram = programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID = OperatingSystem_ObtainAnEntryInTheProcessTable();
	if (PID == NOFREEENTRY)
		return PID;

	// Obtain the memory requirements of the program
	processSize = OperatingSystem_ObtainProgramSize(&programFile, executableProgram->executableName);
	if (processSize == PROGRAMNOTVALID || processSize == PROGRAMDOESNOTEXIST)
		return processSize;

	// Obtain the priority for the process
	priority = OperatingSystem_ObtainPriority(programFile);
	if (priority == PROGRAMNOTVALID)
		return priority;
	// Obtain enough memory space
	int partition = OperatingSystem_ObtainMainMemory(processSize, PID, executableProgram->executableName);

	if (partition < 0)
	{
		return partition;
	}
	// Obtain enough memory space
	loadingPhysicalAddress = partitionsTable[partition].initAddress;

	// Load program in the allocated memory
	int loadProgram = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if (loadProgram == TOOBIGPROCESS)
		return loadProgram;

	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	OperatingSystem_AssignPartition(PID, partition, executableProgram->executableName);

	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(70, INIT, PID, executableProgram->executableName);

	return PID;
}

void OperatingSystem_AssignPartition(int PID, int selectedPartition, char *executableName)
{
	//OperatingSystem_ShowPartitionTable("before allocating memory");

	partitionsTable[selectedPartition].PID = PID;

	ComputerSystem_DebugMessage(111, SYSPROC, PID, processTable[PID].name, statesNames[NEW]);
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(143, SYSMEM, selectedPartition,
								partitionsTable[selectedPartition].initAddress,
								partitionsTable[selectedPartition].size, PID, executableName);
	OperatingSystem_ShowPartitionTable("after allocating memory");
}

void OperatingSystem_ReleaseMainMemory()
{
	int i;
	for (i = 0; i <= PARTITIONTABLEMAXSIZE; i++)
	{
		if (partitionsTable[i].PID == executingProcessID)
		{
			OperatingSystem_ShowPartitionTable("before releasing memory");
			partitionsTable[i].PID = NOPROCESS;
			OperatingSystem_ShowTime(SYSMEM);
			ComputerSystem_DebugMessage(145, SYSMEM, i,
										partitionsTable[i].initAddress, partitionsTable[i].size,
										executingProcessID, processTable[executingProcessID].name);
			OperatingSystem_ShowPartitionTable("after releasing memory");
			break;
		}
	}
}
int OperatingSystem_CheckPartitions(int processSize)
{

	int i = 0, biggestPartition = 0, freePartitions = 0;
	for (i = 0; i < PARTITIONTABLEMAXSIZE; i++)
	{
		if (partitionsTable[i].size >= biggestPartition)
		{
			biggestPartition = partitionsTable[i].size;
		}

		if (partitionsTable[i].size >= processSize && partitionsTable[i].PID == NOPROCESS)
		{
			freePartitions++;
		}
	}
	if (processSize > biggestPartition)
	{
		return TOOBIGPROCESS;
	}
	else if (freePartitions == 0)
	{
		return MEMORYFULL;
	}
	return 1;
}

// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID, char *name)
{
	OperatingSystem_ShowPartitionTable("before allocating memory");

	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(142, SYSMEM, PID, name, processSize);

	int fail = OperatingSystem_CheckPartitions(processSize);
	if (fail < 0)
	{
		return fail;
	}
	else
	{
		int i, selectedPartition = NOPROCESS, size = MAINMEMORYSIZE;
		for (i = 0; i < PARTITIONTABLEMAXSIZE; i++)
		{
			if (partitionsTable[i].PID == NOPROCESS && partitionsTable[i].size >= processSize &&
				partitionsTable[i].size < size)
			{
				size = partitionsTable[i].size;
				selectedPartition = i;
			}
		}
		return selectedPartition;
	}

	return PID * MAINMEMORYSECTIONSIZE;
}

// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex)
{

	processTable[PID].busy = 1;
	processTable[PID].whenToWakeUp = 0;
	processTable[PID].initialPhysicalAddress = initialPhysicalAddress;
	processTable[PID].processSize = processSize;
	processTable[PID].state = NEW;
	processTable[PID].name = programList[processPLIndex]->executableName;
	OperatingSystem_ShowTime(SYSPROC);
	processTable[PID].priority = priority;
	processTable[PID].programListIndex = processPLIndex;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM)
	{
		processTable[PID].copyOfPCRegister = initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister = ((unsigned int)1) << EXECUTION_MODE_BIT;
		processTable[PID].queueID = DAEMONSQUEUE;
	}
	else
	{
		processTable[PID].copyOfPCRegister = 0;
		processTable[PID].copyOfPSWRegister = 0;
		processTable[PID].queueID = USERPROCESSQUEUE;
	}
}

// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID)
{
	int queueID = processTable[PID].queueID;
	if (Heap_add(PID, readyToRunQueue[queueID], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[queueID], PROCESSTABLEMAXSIZE) >= 0)
	{
		char *state = statesNames[processTable[PID].state];
		processTable[PID].state = READY;
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110, SYSPROC, PID,
									processTable[PID].name, state, statesNames[processTable[PID].state]);
		//OperatingSystem_PrintReadyToRunQueue();
	}
}

// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler()
{

	int selectedProcess;
	selectedProcess = OperatingSystem_ExtractFromReadyToRun(USERPROCESSQUEUE);
	if (selectedProcess == NOPROCESS)
		selectedProcess = OperatingSystem_ExtractFromReadyToRun(DAEMONSQUEUE);
	return selectedProcess;
}

// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int queue)
{
	int selectedProcess = NOPROCESS;
	selectedProcess = Heap_poll(readyToRunQueue[queue], QUEUE_PRIORITY, &numberOfReadyToRunProcesses[queue]);
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess;
}

// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID)
{
	// The process identified by PID becomes the current executing process
	executingProcessID = PID;
	// Change the process' state
	char *state = statesNames[processTable[PID].state];
	processTable[PID].state = EXECUTING;
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110, SYSPROC, PID,
								processTable[PID].name, state,
								statesNames[processTable[PID].state]);
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
	//OperatingSystem_PrintStatus();
}

// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID)
{

	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE - 1, processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE - 2, processTable[PID].copyOfPSWRegister);
	Processor_SetAccumulator(processTable[PID].acc);
	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}

// Function invoked when the executing process leaves the CPU
void OperatingSystem_PreemptRunningProcess()
{

	Processor_SetAccumulator(processTable[executingProcessID].acc);
	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID = NOPROCESS;
}

// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID)
{

	processTable[PID].acc = Processor_GetAccumulator();
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE - 1);

	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister = Processor_CopyFromSystemStack(MAINMEMORYSIZE - 2);
}

// Exception management routine
void OperatingSystem_HandleException()
{

	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	OperatingSystem_ShowTime(SYSPROC);

	char *exceptionMessage;
	switch (Processor_GetRegisterB())
	{
	case DIVISIONBYZERO:
		exceptionMessage = "division by zero";
		break;
	case INVALIDINSTRUCTION:
		exceptionMessage = "invalid instruction";
		break;
	case INVALIDADDRESS:
		exceptionMessage = "invalid adress";
		break;
	case INVALIDPROCESSORMODE:
		exceptionMessage = "invalid processor mode";
		break;
	}
	ComputerSystem_DebugMessage(140, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, exceptionMessage);

	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}

// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess()
{

	int selectedProcess;
	char *state = statesNames[processTable[executingProcessID].state];
	processTable[executingProcessID].state = EXIT;
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110, SYSPROC, executingProcessID,
								programList[processTable[executingProcessID].programListIndex]->executableName, state, statesNames[processTable[executingProcessID].state]);
	OperatingSystem_ReleaseMainMemory();
	if (programList[processTable[executingProcessID].programListIndex]->type == USERPROGRAM)
	{
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	}
	if (numberOfNotTerminatedUserProcesses <= 0 && numberOfProgramsInArrivalTimeQueue <= 0)
	{
		if (executingProcessID == sipID)
		{
			OperatingSystem_TerminatingSIP();
			OperatingSystem_ShowTime(SHUTDOWN);
			ComputerSystem_DebugMessage(99, SHUTDOWN, "The system will shut down now...\n");
			return;
		}
		// Simulation must finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess = OperatingSystem_ShortTermScheduler();
	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall()
{

	int systemCallID;
	int firstPid;
	int queueExecuting = processTable[executingProcessID].queueID;
	int prev;
	// Register A contains the identifier of the issued system call
	systemCallID = Processor_GetRegisterA();

	switch (systemCallID)
	{
	case SYSCALL_PRINTEXECPID:
		// Show message: "Process [executingProcessID] has the processor assigned\n"
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(72, SYSPROC, executingProcessID,
									programList[processTable[executingProcessID].programListIndex]->executableName);

		break;

	case SYSCALL_END:
		// Show message: "Process [executingProcessID] has requested to terminate\n"
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(73, SYSPROC, executingProcessID,
									programList[processTable[executingProcessID].programListIndex]->executableName);
		OperatingSystem_TerminateProcess();
		OperatingSystem_PrintStatus();
		break;

	case SYSCALL_YIELD:
		if (numberOfReadyToRunProcesses[queueExecuting] > 0)
		{
			firstPid = Heap_getFirst(readyToRunQueue[queueExecuting], numberOfReadyToRunProcesses[queueExecuting]);
			if (processTable[executingProcessID].priority == processTable[firstPid].priority)
			{
				OperatingSystem_ShowTime(SYSPROC);
				ComputerSystem_DebugMessage(115, SHORTTERMSCHEDULE, executingProcessID,
											processTable[executingProcessID].name,
											firstPid, processTable[firstPid].name);
				OperatingSystem_PreemptRunningProcess();
				firstPid = OperatingSystem_ExtractFromReadyToRun(queueExecuting);
				OperatingSystem_Dispatch(firstPid);
				OperatingSystem_PrintStatus();
			}
		}
		break;

	case SYSCALL_SLEEP:
		prev = executingProcessID;
		OperatingSystem_SaveContext(prev);
		OperatingSystem_MoveToTheBLOCKEDState(prev);
		OperatingSystem_Dispatch(OperatingSystem_ShortTermScheduler());
		OperatingSystem_PrintStatus();
		break;

	default:
		ComputerSystem_DebugMessage(141, SYSPROC, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, systemCallID);
		OperatingSystem_TerminateProcess();
		OperatingSystem_PrintStatus();
	}
}
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint)
{
	switch (entryPoint)
	{
	case SYSCALL_BIT: // SYSCALL_BIT=2
		OperatingSystem_HandleSystemCall();
		break;
	case EXCEPTION_BIT: // EXCEPTION_BIT=6
		OperatingSystem_HandleException();
		break;
	case CLOCKINT_BIT: // CLOCKINT_BIT=9
		OperatingSystem_HandleClockInterrupt();
		break;
	}
}

void OperatingSystem_PrintReadyToRunQueue()
{
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);
	int i;

	for (i = 0; i < NUMBEROFQUEUES; i++)
	{
		OperatingSystem_PrintQueue(i);
	}
}

void OperatingSystem_PrintQueue(int queue)
{
	int i, PID, priority;
	int size = numberOfReadyToRunProcesses[queue];
	if (size == 0)
	{
		ComputerSystem_DebugMessage(109, SHORTTERMSCHEDULE, queueNames[queue], "\n");
	}
	else
	{
		ComputerSystem_DebugMessage(109, SHORTTERMSCHEDULE, queueNames[queue], "");
		for (i = 0; i < size; i++)
		{
			PID = readyToRunQueue[queue][i].info;
			priority = processTable[PID].priority;
			if (i == 0)
			{
				if (size == 1)
				{
					ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, PID,
												priority, "\n");
				}
				else
				{
					ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, PID,
												priority, ", ");
				}
			}
			else if (i == size - 1)
			{
				ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, PID,
											priority, "\n");
			}
			else
			{
				ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, PID,
											priority, ", ");
			}
		}
	}
}

void OperatingSystem_HandleClockInterrupt()
{
	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(116, INTERRUPT, numberOfClockInterrupts);

	int numWakeUpProcess = 0;
	int selectedProcess = 0;
	while (numberOfSleepingProcesses > 0 && processTable[Heap_getFirst(sleepingProcessesQueue, numberOfSleepingProcesses)].whenToWakeUp == numberOfClockInterrupts)
	{
		selectedProcess = Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses);
		OperatingSystem_MoveToTheREADYState(selectedProcess);
		numWakeUpProcess++;
	}
	int new = OperatingSystem_LongTermScheduler();
	if (numWakeUpProcess > 0 || new > 0)
	{
		OperatingSystem_PrintStatus();
		selectedProcess = OperatingSystem_ShortTermScheduler();
		if (OperatingSystem_isImportant(selectedProcess, executingProcessID))
		{
			OperatingSystem_ChangeProcess(selectedProcess);
		}

		else
		{
			Heap_add(selectedProcess, readyToRunQueue[processTable[selectedProcess].queueID], QUEUE_PRIORITY,
					 &numberOfReadyToRunProcesses[processTable[selectedProcess].queueID], PROCESSTABLEMAXSIZE);
		}
	}

	return;
}

int OperatingSystem_isImportant(int p1, int p2)
{
	if (processTable[p1].queueID < processTable[p2].queueID)
		return 1;
	if (processTable[p1].priority < processTable[p2].priority)
		return 1;
	return 0;
}

void OperatingSystem_ChangeProcess(int selectedProcess)
{
	int copyOfExecutingProcessID = executingProcessID;
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(121, SHORTTERMSCHEDULE, copyOfExecutingProcessID, processTable[copyOfExecutingProcessID].name, selectedProcess, processTable[selectedProcess].name);
	OperatingSystem_PreemptRunningProcess();
	OperatingSystem_Dispatch(selectedProcess);
	OperatingSystem_PrintStatus();
}

void OperatingSystem_MoveToTheBLOCKEDState(int PID)
{
	char *lastState = statesNames[processTable[PID].state];
	int accum = Processor_GetAccumulator();
	int value = abs(accum) + numberOfClockInterrupts + 1;
	processTable[PID].whenToWakeUp = value;
	if (Heap_add(PID, sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses, PROCESSTABLEMAXSIZE) >= 0)
	{
		processTable[PID].state = BLOCKED;
		OperatingSystem_SaveContext(PID);
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110, SYSPROC, PID, processTable[PID].name,
									lastState, statesNames[processTable[PID].state]);
	}
}

int OperatingSystem_ExtractFromBlockedToReady()
{
	return Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses);
}

int OperatingSystem_getExecutingProcessID()
{
	return executingProcessID;
}