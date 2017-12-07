/*
	Author: Ali Arda Eker
	Date: 12/04/17
	
	Computer Organization & Architecture Fall 17
	Final Project
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <vector>

#define MEM_SIZE 4000
#define RF_SIZE 16
#define PRF_SIZE 32
#define IQ_SIZE 16
#define DATA_SIZE 4

using namespace std;

struct PSWRegister
{
	int busy;
	int zero;
	bool carry;
	bool negative;
} PSWRegister;

typedef struct Instruction
{
	int literal;
	int adressForMemory;
	int dataToBeWritten; 

	int pc;
	char opcode[10];
	int srcRegisters[2];
	int destRegister;
	char rawData[40];

	int pSrcRegValue[2];
	int pDesRegValue;

	int pSrcRegNum[2];
	int pDesRegNum;

	bool pSrcRegHasData[2];

	bool branchTaken;
} Instruction;

struct CodeSegment
{
	int size;
	// Filled in initialization by reading input file
	Instruction codeArray[MEM_SIZE]; 
} CodeSegment;

struct Stage
{
	Instruction inputI;
	bool inputFlag;
} Fetch, Decode, MUL1, MUL2, INT_FU, DU1, DU2, DU3, DU4, IQ_STAGE, COMMIT, MEM; 

// Core pipeline loop is here. Also implements the command promt
void startSimulation();

// Inititalization function.
// State, data memory, code memory and register file are created. 
void initialize(FILE * f);

// 12 Functional Units
bool fetch();
void decode();
void memoryAccess();
void writeBack();
void mul1();
void mul2();
void intFU();
void div1();
void div2();
void div3();
void div4();
void mem();

void IQ_stage();

// Print function for Display command
// Stages, register and data memory are shown
void print();
void generateNOP(int which_stage);
void flush();
void searchIQtoForward (int pDesReg, int writeThis);
void forwardToIQ ();
int searchIQtoRetrieveI (int which_way);
void commit();
void bypassCheck();

typedef struct Register
{
	int value;
   	int valid;
} Register;

Register registerArray[RF_SIZE];
int dataArray[MEM_SIZE];

typedef struct PhysicalRegister
{
	int value;
   	int correspondingArchReg;
   	bool hasData;
   	int valid;
   	bool holdsDes;
} PhysicalRegister;

PhysicalRegister physicalRF[PRF_SIZE];

vector <Instruction> IQ;
vector <Instruction> commit_vector;

typedef struct ROB_entry
{
	Instruction instruction;
	bool readyToCommit;
} ROB_entry;

vector <ROB_entry> ROB;
vector <Instruction> LSQ; 

bool NOP_Flag = false;
int HALT_Flag = 0;
int PC = 0;
int new_PC = 0;

int main() 
{
    startSimulation();
    return 0;
}

void startSimulation()
{
	FILE * fp;
	bool flag = true;
	char input[100];

	printf("\nEnter the name of ASCII file that contains the code to be simulated:\n");

	while (flag)
	{
		flag = false;
		scanf("%s", input);

		if ((fp = fopen(input, "r")) == NULL) 
		{
        	printf("Error: File cannot be opened. Try again:\n");
        	flag = true;
        }
        else printf("File successfully opened.\n\n");
	}

	initialize(fp);
    fclose(fp);
	flag = true;

	fseek(stdin, 0, SEEK_END);
	printf("Enter `sim <num_cycles>` to simulate APEX with given number of cycles.\n");
	scanf("%[^\n]s", input);
	int total_cycle = 0;

	while (flag)
	{
		char * cmd;
		
  		cmd = strtok(input, " ");
		if (strcmp(input, "sim") == 0)
		{
			cmd = strtok(NULL, "\n");

			if (cmd != NULL)
			{
				int number_of_cycles = atoi(cmd), counter = 0;
				printf("\nSimulating %d cycles ...\n", number_of_cycles);
				
				// Core pipeline loop
				while (counter < number_of_cycles)
				{
					if (NOP_Flag == true) flush();
					
					mem();

					bypassCheck();
					commit();

					intFU();
					mul2();
					mul1();
					div4();
					div3();
					div2();	
					div1();

					IQ_stage();
					forwardToIQ();

					decode();
					if (!fetch()) break;
				
					printf("\nCycle %d\n", total_cycle + 1);		
					counter++;
					total_cycle++;
					print();
				}

				if (counter == number_of_cycles) printf("%d cycles simulated.\n", total_cycle);
				else break;
			}
			else printf("Error: Invalid command.\n");
		}
		else printf("Error: Invalid command.\n");

        fseek(stdin, 0, SEEK_END);
		printf("\n--> sim <num_cycles>\n");
		scanf("%[^\n]s", input);
	}

	printf("Simulation has ended by HALT after %d`th cycle.\n\n", total_cycle);
}

void initialize(FILE * f)
{
	for (int i = 0; i < PRF_SIZE; i++) 
	{
   		physicalRF[i].value = 0;
   		physicalRF[i].correspondingArchReg = -2;
   		physicalRF[i].valid = 0;
		physicalRF[i].hasData = false;
		physicalRF[i].holdsDes = false;
	}

	Fetch.inputFlag = true;

	Instruction i;
	strcpy(i.rawData, "");
	MEM.inputI = i;

	for (int i = 0; i < MEM_SIZE; i++) dataArray[i] = 0;

	for (int i = 0; i < RF_SIZE; i++) 
	{
		registerArray[i].value = -2;
		registerArray[i].valid = 0;
	}

	PSWRegister.busy = 0;
	PSWRegister.negative = false;
	PSWRegister.carry = false;
	PSWRegister.zero = -1;

	char instruction[40];
	int k = 0;

	while (fgets(instruction, sizeof(instruction), f) != NULL)
	{
		// QUESTION: should it be dynamically alocated?
		if (k > 999) assert(false && "APEX crushed due to Code Segment overflow.");
  		strcpy(CodeSegment.codeArray[k * DATA_SIZE].rawData, instruction);
  		k++;
	}

	CodeSegment.size = k *  DATA_SIZE;
}

bool fetch()
{
	if (HALT_Flag > 0)
	{
		generateNOP(0);

		HALT_Flag++;

		// If Halt reached WB stage, quit the simulation
		if (HALT_Flag == 9) return false;
		else return true;
	}

	Instruction * instruction = (Instruction *) malloc(sizeof(Instruction));

	// If there is instruction in code memory to be fetched
	if (CodeSegment.size > PC) 
	{
		strcpy(instruction -> rawData, CodeSegment.codeArray[PC].rawData);

		instruction -> pc = PC;
		instruction -> pSrcRegHasData[0] = false;
		instruction -> pSrcRegHasData[1] = false;
		
		instruction -> pSrcRegValue[0] = -2;
		instruction -> pSrcRegValue[1] = -2;
		instruction -> pDesRegValue = -2;

		instruction -> pSrcRegNum[0] = -2;
		instruction -> pSrcRegNum[1] = -2;
		instruction -> pDesRegNum = -2;
		instruction -> branchTaken = false;
		instruction -> adressForMemory = -2;

		// Parsing the raw data
		char * tmp = strdup(instruction -> rawData);
  		char * token = strtok(tmp, ",\n");
  		char * token2, * token3, * token4;

  		strcpy(instruction -> opcode, token);

  		if (!strcmp(token, "HALT")) 
  		{
  			instruction -> srcRegisters[0] = -2;
			instruction -> srcRegisters[1] = -2;	
			instruction -> destRegister = -2;
  		}
  		else if (!strcmp(token, "MOVC"))
  		{
  			token2 = strtok(NULL, ",\n");
			token3 = strtok(NULL, ",\n");

			instruction -> srcRegisters[0] = -2;
			instruction -> srcRegisters[1] = -2;	
			instruction -> destRegister = atoi(&token2[1]);
			instruction -> literal = atoi(&token3[1]);
  		}
  		else if	(!strcmp(token, "JUMP"))
		{
			token2 = strtok(NULL, ",\n");
			token3 = strtok(NULL, ",\n");

			instruction -> srcRegisters[0] = atoi(&token2[1]);
			instruction -> literal = atoi(&token3[1]);
			instruction -> srcRegisters[1] = -2;
			instruction -> destRegister = -2;
			instruction -> branchTaken = true;
		}
		else if (!strcmp(token, "BNZ") ||
			  	  !strcmp(token, "BZ")) 
		{
			instruction -> srcRegisters[0] = -2;
			instruction -> srcRegisters[1] = -2;
			token2 = strtok(NULL, ",\n");
			instruction -> literal = atoi(&token2[1]);
			instruction -> destRegister = -2;
		}
		else if (!strcmp(token, "STORE"))
		{
			token2 = strtok(NULL, ",\n");
			token3 = strtok(NULL, ",\n");
			token4 = strtok(NULL, ",\n");

			instruction -> srcRegisters[0] = atoi(&token2[1]);
			instruction -> srcRegisters[1] = atoi(&token3[1]);	
			instruction -> literal = atoi(&token4[1]);
			instruction -> destRegister = -2;
		}
		else if (!strcmp(token, "LOAD") ||
				 !strcmp(token , "JAL"))
		{
			token2 = strtok(NULL, ",\n");
			token3 = strtok(NULL, ",\n");
			token4 = strtok(NULL, ",\n");

			instruction -> destRegister = atoi(&token2[1]);
			instruction -> srcRegisters[0] = atoi(&token3[1]);	
			instruction -> literal = atoi(&token4[1]);
			instruction -> srcRegisters[1] = -2;

			if (!strcmp(token , "JAL")) instruction -> branchTaken = true;
		}
		else
		{
			token2 = strtok(NULL, ",\n");
			token3 = strtok(NULL, ",\n");
			token4 = strtok(NULL, ",\n");
			
			instruction -> destRegister = atoi(&token2[1]);
			instruction -> srcRegisters[0] = atoi(&token3[1]);
			instruction -> srcRegisters[1] = atoi(&token4[1]);	
		}
	}
	// Otherwise just behave like fetching empty data.
	// Similation will last until a Halt is encountered
	else 
	{
		strcpy(instruction -> rawData, " - ");
		instruction -> destRegister = -2;
		instruction -> srcRegisters[0] = -2;
		instruction -> srcRegisters[1] = -2;
		instruction -> pSrcRegHasData[0] = false;
		instruction -> pSrcRegHasData[1] = false;
		
		instruction -> pSrcRegValue[0] = -2;
		instruction -> pSrcRegValue[1] = -2;
		instruction -> pDesRegValue = -2;

		instruction -> pSrcRegNum[0] = -2;
		instruction -> pSrcRegNum[1] = -2;
		instruction -> pDesRegNum = -2;
		instruction -> branchTaken = false;
		instruction -> adressForMemory = -2;
	}

	Fetch.inputI = * instruction;
	PC = PC + DATA_SIZE;
	Decode.inputFlag = true;

	// If Decode is stalled, stall the Fetch also 
	//if (Decode.stalled == true) Fetch.stalled = true;
	
	return true;
}

void decode()
{
	// Fetch will turn this flag to true
	if (Decode.inputFlag == false) return;

	Decode.inputI = Fetch.inputI;
	int targetPhysicalIndex, i, j;
	bool found;

	if (!strcmp(Decode.inputI.rawData, " - ") ||
		!strcmp(Decode.inputI.rawData, "")) return;

	// Assign physical registers for architecture source registers
	for (i = 0; i < 2; i++)
	{
		found = false;

		if (Decode.inputI.srcRegisters[i] != -2)
		{
			// If a same des reg renamed itself before this instruction take it
			for (j = PRF_SIZE - 1; j >= 0; j--)
			{
				if (physicalRF[j].hasData &&
					physicalRF[j].holdsDes &&
					(physicalRF[j].correspondingArchReg == Decode.inputI.srcRegisters[i]))
				{
					Decode.inputI.pSrcRegNum[i] = j;
					found = true;

					break;
				}
			}

			if (found) continue;

			for (j = 0; j < PRF_SIZE; j++)
			{
				if (!physicalRF[j].hasData)
				{
					physicalRF[j].correspondingArchReg = Decode.inputI.srcRegisters[i];
					Decode.inputI.pSrcRegNum[i] = j;
					physicalRF[j].hasData = true;
					physicalRF[j].holdsDes = false;

					break;
				}
			}
		}
	}

	// Assign physical registers for architecture destination registers
	if (Decode.inputI.destRegister != -2)
	{
		for (i = 0; i < PRF_SIZE; i++)
		{
			if (physicalRF[i].hasData == false) 
			{
				physicalRF[i].hasData = true;
				physicalRF[i].holdsDes = true;
				physicalRF[i].correspondingArchReg = Decode.inputI.destRegister;
				Decode.inputI.pDesRegNum = i;
				break;
			}
		}
	}

	if (targetPhysicalIndex == 32) printf("Physical Register is Full !!!");

  	if (!strcmp(Decode.inputI.opcode, "HALT")) HALT_Flag++;
  	
  	else if (!strcmp(Decode.inputI.opcode, "MOVC")) {}

	else if (!strcmp(Decode.inputI.opcode, "LOAD") ||
		     !strcmp(Decode.inputI.opcode, "JAL"))
	{
		if (registerArray[Decode.inputI.srcRegisters[0]].value != -2)
		{
			Decode.inputI.pSrcRegValue[0] = registerArray[Decode.inputI.srcRegisters[0]].value;
			Decode.inputI.pSrcRegHasData[0] = true;
		}
		else if (physicalRF[Decode.inputI.pSrcRegNum[0]].valid == 0)
		{	
			Decode.inputI.pSrcRegValue[0] = physicalRF[Decode.inputI.pSrcRegNum[0]].value;
			Decode.inputI.pSrcRegHasData[0] = true;
		}	
	}
	else if (!strcmp(Decode.inputI.opcode, "EXOR") ||
			  !strcmp(Decode.inputI.opcode, "AND") ||
			  !strcmp(Decode.inputI.opcode, "MUL") ||
			  !strcmp(Decode.inputI.opcode, "DIV") ||
			  !strcmp(Decode.inputI.opcode, "ADD") ||
			  !strcmp(Decode.inputI.opcode, "SUB") ||
			!strcmp(Decode.inputI.opcode, "STORE") ||
			   !strcmp(Decode.inputI.opcode, "OR"))
	{
		for (int i = 0; i < 2; i++)
		{
			if (registerArray[Decode.inputI.srcRegisters[i]].value != -2)
			{
				Decode.inputI.pSrcRegValue[i] = registerArray[Decode.inputI.srcRegisters[i]].value;
				Decode.inputI.pSrcRegHasData[i] = true;
			}
			else if (physicalRF[Decode.inputI.pSrcRegNum[i]].valid == 0)
			{	
				Decode.inputI.pSrcRegValue[i] = physicalRF[Decode.inputI.pSrcRegNum[i]].value;
				Decode.inputI.pSrcRegHasData[i] = true;
			}
		}

		// Change destination register`s status as invalid

		if (!strcmp(Decode.inputI.opcode, "MUL") ||
			!strcmp(Decode.inputI.opcode, "DIV") ||
			!strcmp(Decode.inputI.opcode, "ADD") ||
			!strcmp(Decode.inputI.opcode, "SUB")) 
			{
				PSWRegister.busy++;
				//printf("BUSY ++ by %s, opcode %s: %d\n", Decode.inputI.rawData, Decode.inputI.opcode, PSWRegister.busy);
			}
	}
	else if (!strcmp(Decode.inputI.opcode, "JUMP"))
	{
		if (registerArray[Decode.inputI.srcRegisters[0]].value != -2)
		{
			Decode.inputI.pSrcRegValue[0] = registerArray[Decode.inputI.srcRegisters[0]].value;
			Decode.inputI.pSrcRegHasData[0] = true;
		}
		else if (physicalRF[Decode.inputI.pSrcRegNum[0]].valid == 0)
		{	
			Decode.inputI.pSrcRegValue[0] = physicalRF[Decode.inputI.pSrcRegNum[0]].value;
			Decode.inputI.pSrcRegHasData[0] = true;
		}
	}
	
	if (Decode.inputI.pDesRegNum != -2) physicalRF[Decode.inputI.pDesRegNum].valid++;
	IQ_STAGE.inputFlag = true;
}

void IQ_stage()
{
	if (IQ_STAGE.inputFlag == false) return;

	if (strcmp(Decode.inputI.rawData, " - ") && strcmp(Decode.inputI.rawData, ""))
	{
		IQ.push_back(Decode.inputI);

		ROB_entry r;
		r.instruction = Decode.inputI;
		r.readyToCommit = false;
		ROB.push_back(r);
	
		if (!strcmp(Decode.inputI.opcode, "LOAD") || 
			!strcmp(Decode.inputI.opcode, "STORE")) IQ.push_back(Decode.inputI);;
	}

	MUL1.inputFlag = true;
	INT_FU.inputFlag = true;
	DU1.inputFlag = true;
}

// 0 for INT_FU, 1 for MUL, 2 for DIV
// return the index of available Instruction in IQ, otherwise -1
int searchIQtoRetrieveI(int which_way)
{
	if (which_way == 0)
	{
		for (int i = 0; i < IQ.size(); i++)
		{
			if (!strcmp(IQ[i].opcode, "BNZ") || !strcmp(IQ[i].opcode, "BZ"))
			{
				//printf("busy: %d\n", PSWRegister.busy);

				if (PSWRegister.busy == 0) return i;
			}

			else if (strcmp(IQ[i].opcode, "DIV") 
					&& strcmp(IQ[i].opcode, "MUL")
					&& strcmp(IQ[i].opcode, "HALT"))
			{
				//printf("%s has %d and %d\n", IQ[i].opcode, 
				//						IQ[i].pSrcRegHasData[0], 
				//						IQ[i].pSrcRegHasData[1]);

				if (IQ[i].pSrcRegNum[0] == -2 &&
					IQ[i].pSrcRegNum[1] == -2) return i;

				if (IQ[i].pSrcRegHasData[0] &&
					IQ[i].pSrcRegNum[1] == -2) return i;

				if (IQ[i].pSrcRegHasData[0] &&
					IQ[i].pSrcRegHasData[1]) return i;
			}
		}
	}
	else if (which_way == 1)
	{
		for (int i = 0; i < IQ.size(); i++)
		{
			if (!strcmp(IQ[i].opcode, "MUL"))
			{
				if (IQ[i].pSrcRegHasData[0] &&
					IQ[i].pSrcRegHasData[1]) return i;
			}
		}
	}
	else if (which_way == 2)
	{
		for (int i = 0; i < IQ.size(); i++)
		{
			if (!strcmp(IQ[i].opcode, "DIV") || 
				 !strcmp(IQ[i].opcode, "HALT"))
			{
				//printf("%s has %d and %d\n", IQ[i].opcode, 
				//						IQ[i].pSrcRegHasData[0], 
				//						IQ[i].pSrcRegHasData[1]);

				// For Halt
				if (IQ[i].pSrcRegNum[0] == -2 &&
					IQ[i].pSrcRegNum[1] == -2) return i;

				if (IQ[i].pSrcRegHasData[0] &&
					IQ[i].pSrcRegHasData[1]) return i;
			}
		}
	}

	return -1;
}

void div1()
{
	// Decode will turn this flag to true
	if (DU1.inputFlag == false) return;

	int iqIndex = searchIQtoRetrieveI(2);

	if (iqIndex != -1)
	{
		DU1.inputI = IQ[iqIndex];
		IQ.erase(IQ.begin() + iqIndex);
	}
	else generateNOP(5);

	DU2.inputFlag = true;
}

void div2()
{
	if (DU2.inputFlag == false) return;
	DU2.inputI = DU1.inputI;

	DU3.inputFlag = true;
}

void div3()
{
	if (DU3.inputFlag == false) return;
	DU3.inputI = DU2.inputI;

	DU4.inputFlag = true;
}

void div4()
{
	if (DU4.inputFlag == false) return;
	DU4.inputI = DU3.inputI;

	//	Actual division operation takes place here.
	if (!strcmp(DU4.inputI.opcode, "DIV")) 
	{
		//printf("Div sources: %d - %d \n", DU4.inputI.pSrcRegValue[0], DU4.inputI.pSrcRegValue[1]);
		DU4.inputI.dataToBeWritten = floor(DU4.inputI.pSrcRegValue[0] / DU4.inputI.pSrcRegValue[1]);

		// Update zero flag according to the result of the division
		if (DU4.inputI.dataToBeWritten == 0) PSWRegister.zero = 1;
		else PSWRegister.zero = 0;
	
		physicalRF[DU4.inputI.pDesRegNum].value = DU4.inputI.dataToBeWritten;
		physicalRF[DU4.inputI.pDesRegNum].valid--;
		PSWRegister.busy--;

		//printf("BUSY -- : %d\n", PSWRegister.busy);
	}

	char * raw, * rawDU = DU4.inputI.rawData;;

	for (int i = 0; i < ROB.size(); i++)
	{
		raw = ROB[i].instruction.rawData;

		if (!strcmp(raw, rawDU)) ROB[i].readyToCommit = true;
	}

	for (int i = ROB.size() - 1; i >= 0; i--)
	{
		raw = ROB[i].instruction.opcode;

		if (!strcmp(raw, "BZ") && PSWRegister.zero)
		{
			ROB[i].instruction.branchTaken = true;
			break;
		}
		else if (!strcmp(raw, "BNZ") && !PSWRegister.zero)
		{
			ROB[i].instruction.branchTaken = true;
			break;
		}
	}


	COMMIT.inputFlag = true;
}

void mul1()
{
	// Decode will turn this flag to true
	if (MUL1.inputFlag == false) return;

	int iqIndex = searchIQtoRetrieveI(1);

	if (iqIndex != -1)
	{
		MUL1.inputI = IQ[iqIndex];
		IQ.erase(IQ.begin() + iqIndex);
	}
	else generateNOP(3);

	MUL2.inputFlag = true;
}

void mul2()
{
	// Mul 1 will turn this flag to on
	if (MUL2.inputFlag == false) return;

	MUL2.inputI = MUL1.inputI;
	
	//	Actual multipication operation takes place here.
	if (!strcmp(MUL2.inputI.opcode, "MUL")) 
	{
		MUL2.inputI.dataToBeWritten = MUL2.inputI.pSrcRegValue[0] * MUL2.inputI.pSrcRegValue[1];

		// Update zero flag according to the result of the multiplication
		if (MUL2.inputI.dataToBeWritten == 0) PSWRegister.zero = 1;
		else PSWRegister.zero = 0;
	
		physicalRF[MUL2.inputI.pDesRegNum].value = MUL2.inputI.dataToBeWritten;
		physicalRF[MUL2.inputI.pDesRegNum].valid--;
		
		PSWRegister.busy--;

		//printf("BUSY -- : %d\n", PSWRegister.busy);

		char * raw;
		char * rawMUL = MUL2.inputI.rawData;

		for (int i = 0; i < ROB.size(); i++)
		{
			raw = ROB[i].instruction.rawData;

			if (!strcmp(raw, rawMUL)) ROB[i].readyToCommit = true;
		}

		for (int i = ROB.size() - 1; i >= 0; i--)
		{
			raw = ROB[i].instruction.opcode;

			if (!strcmp(raw, "BZ") && PSWRegister.zero)
			{
				ROB[i].instruction.branchTaken = true;
				break;
			}
			else if (!strcmp(raw, "BNZ") && !PSWRegister.zero)
			{
				ROB[i].instruction.branchTaken = true;
				break;
			}
		}
	}

	COMMIT.inputFlag = true;
}

void intFU()
{
	if (INT_FU.inputFlag == false) return;

	int iqIndex = searchIQtoRetrieveI(0);

	//printf("iq Index: %d\n", iqIndex);

	if (iqIndex != -1)
	{
		INT_FU.inputI = IQ[iqIndex];
		IQ.erase(IQ.begin() + iqIndex);
	}
	else generateNOP(2);

	if (!strcmp(INT_FU.inputI.opcode, "BNZ"))
	{
		// If the result of last arithmetic operation is not zero BNZ 
		// transfers the control to the new location shown by its literal
		if (PSWRegister.zero == 0) 
		{
			new_PC = INT_FU.inputI.pc + INT_FU.inputI.literal;
			NOP_Flag = true;
		}
	}
	else if (!strcmp(INT_FU.inputI.opcode, "BZ"))
	{
		// If the result of last arithmetic operation is zero BZ 
		// transfers the control to the new location shown by its literal
		if (PSWRegister.zero == 1) 
		{
			new_PC = INT_FU.inputI.pc + INT_FU.inputI.literal;
			NOP_Flag = true;
		}
	}
	
	else if (!strcmp(INT_FU.inputI.opcode, "STORE"))
	{INT_FU.inputI.adressForMemory = INT_FU.inputI.pSrcRegValue[1] + INT_FU.inputI.literal;}
	
	else if (!strcmp(INT_FU.inputI.opcode, "LOAD"))
	{INT_FU.inputI.adressForMemory = INT_FU.inputI.pSrcRegValue[0] + INT_FU.inputI.literal;}
	
	else if (!strcmp(INT_FU.inputI.opcode, "ADD"))
	{
		INT_FU.inputI.dataToBeWritten = INT_FU.inputI.pSrcRegValue[0] + INT_FU.inputI.pSrcRegValue[1];
		// Updates zero flag
		if (INT_FU.inputI.dataToBeWritten == 0) PSWRegister.zero = 1;
		else PSWRegister.zero = 0;

		// Update physical register
		physicalRF[INT_FU.inputI.pDesRegNum].value = INT_FU.inputI.dataToBeWritten;
		PSWRegister.busy--;

		//printf("BUSY -- : %d\n", PSWRegister.busy);
	}
	else if (!strcmp(INT_FU.inputI.opcode, "SUB"))
	{
		INT_FU.inputI.dataToBeWritten = INT_FU.inputI.pSrcRegValue[0] - INT_FU.inputI.pSrcRegValue[1];
		// Updates zero flag
		if (INT_FU.inputI.dataToBeWritten == 0) PSWRegister.zero = 1;
		else PSWRegister.zero = 0;

		// Update physical register
		physicalRF[INT_FU.inputI.pDesRegNum].value = INT_FU.inputI.dataToBeWritten;
		PSWRegister.busy--;

		//printf("BUSY -- : %d\n", PSWRegister.busy);
	}
	else if (!strcmp(INT_FU.inputI.opcode, "EXOR")) 
	{
		INT_FU.inputI.dataToBeWritten = INT_FU.inputI.pSrcRegValue[0] ^ INT_FU.inputI.pSrcRegValue[1];
		// Update physical register
		physicalRF[INT_FU.inputI.pDesRegNum].value = INT_FU.inputI.dataToBeWritten;
	}
	else if (!strcmp(INT_FU.inputI.opcode, "OR")) 
	{
		INT_FU.inputI.dataToBeWritten = INT_FU.inputI.pSrcRegValue[0] | INT_FU.inputI.pSrcRegValue[1];
		// Update physical register
		physicalRF[INT_FU.inputI.pDesRegNum].value = INT_FU.inputI.dataToBeWritten;
	}
	else if (!strcmp(INT_FU.inputI.opcode, "AND")) 
	{
		INT_FU.inputI.dataToBeWritten = INT_FU.inputI.pSrcRegValue[0] & INT_FU.inputI.pSrcRegValue[1];
		// Update physical register
		physicalRF[INT_FU.inputI.pDesRegNum].value = INT_FU.inputI.dataToBeWritten;
	}
	else if (!strcmp(INT_FU.inputI.opcode, "MOVC")) 
	{
		//printf("INT_FU.inputI.pDesReg: %d -- INT_FU.inputI.literal: %d \n", INT_FU.inputI.pDesReg, INT_FU.inputI.literal);
		physicalRF[INT_FU.inputI.pDesRegNum].value = INT_FU.inputI.literal;
	}
	else if (!strcmp(INT_FU.inputI.opcode, "JUMP") ||
			 !strcmp(INT_FU.inputI.opcode, "JAL")) 
	{
		if (!strcmp(INT_FU.inputI.opcode, "JAL")) 
		{
			int calculatedData = INT_FU.inputI.pc + DATA_SIZE + MEM_SIZE;
			physicalRF[INT_FU.inputI.pDesRegNum].value = calculatedData;
		}

		INT_FU.inputI.dataToBeWritten = INT_FU.inputI.pSrcRegValue[0] + INT_FU.inputI.literal;
		new_PC = INT_FU.inputI.dataToBeWritten - MEM_SIZE;
		NOP_Flag = true;
	}

	if (INT_FU.inputI.destRegister != -2) physicalRF[INT_FU.inputI.pDesRegNum].valid--;

	COMMIT.inputFlag = true;

	char * rawINT = INT_FU.inputI.rawData;
	char * raw;

	for (int i = 0; i < ROB.size(); i++)
	{
		raw = ROB[i].instruction.rawData;

		if (!strcmp(raw, rawINT))
		{
			if (strcmp(INT_FU.inputI.opcode, "LOAD")) ROB[i].readyToCommit = true;

			if (!strcmp(INT_FU.inputI.opcode, "STORE") ||
				!strcmp(INT_FU.inputI.opcode, "LOAD"))
			{ROB[i].instruction.adressForMemory = INT_FU.inputI.adressForMemory;}
		}
	}

	for (int i = 0; i < LSQ.size(); i++)
	{
		raw = LSQ[i].rawData;

		//printf("lsq: %s\n", raw);
		//printf("int: %s\n", rawINT);

		if (!strcmp(raw, rawINT)) LSQ[i].adressForMemory = INT_FU.inputI.adressForMemory;
	}

	for (int i = ROB.size() - 1; i >= 0; i--)
	{
		raw = ROB[i].instruction.opcode;

		if (!strcmp(raw, "BZ") && PSWRegister.zero)
		{
			ROB[i].instruction.branchTaken = true;
			break;
		}
		else if (!strcmp(raw, "BNZ") && !PSWRegister.zero)
		{
			ROB[i].instruction.branchTaken = true;
			break;
		}
	}
}

void bypassCheck()
{
	bool control = false;

	for (int i = 0; i < LSQ.size(); i++)
	{
		//printf("opcede: %s, adress: %d\n", LSQ[i].opcode, LSQ[i].adressForMemory);

		if (!strcmp(LSQ[i].opcode, "LOAD") &&
			LSQ[i].adressForMemory != -2)
		{
			for (int j = i - 1; j >= 0; j--)
			{
				//printf("opcede: %s, adress: %d, adress: %d\n", LSQ[j].opcode, LSQ[j].adressForMemory, LSQ[i].adressForMemory);

				// Check for Load to pick up an early Store`s value
				if (!strcmp(LSQ[j].opcode, "STORE") &&
					LSQ[j].adressForMemory != -2 &&
					LSQ[j].adressForMemory == LSQ[i].adressForMemory)
				{
					int value = LSQ[j].pSrcRegValue[0];

					if (value != -2)
					{
						//printf("LOAD PICK UP\n");

						LSQ[i].pDesRegValue = value;
						commit_vector.push_back(LSQ[i]);

						registerArray[LSQ[i].destRegister].value = value;
						physicalRF[LSQ[i].pDesRegNum].value = value;

						// Remove from ROB
						for (int k = 0; k < ROB.size(); k++)
						{if (!strcmp(ROB[k].instruction.rawData, LSQ[i].rawData)) ROB.erase(ROB.begin() + k);}

						// Remove from LSQ
						LSQ.erase(LSQ.begin() + i);

						control = true;
						break;
					}
				}
			}
		}

		if (control) break;
	}

	// If pick up not happened check for by pass
	if (!control)
	{
		for (int i = 0; i < LSQ.size(); i++)
		{
			if (!strcmp(LSQ[i].opcode, "LOAD") &&
				LSQ[i].adressForMemory != -2)
			{
				for (int j = 0; j < i; j++)
				{
					if (!strcmp(LSQ[j].opcode, "STORE") &&
						LSQ[j].adressForMemory != -2 &&
						LSQ[j].adressForMemory != LSQ[i].adressForMemory)
					{
						//printf("LOAD BY PASS\n");

						Instruction load_jumping = LSQ[i];
						//LSQ.push_back(load_jumping);

						LSQ.insert(LSQ.begin() + j, load_jumping);
						LSQ.erase(LSQ.begin() + i + 1);

						control = true;
						break;
					}	
				}
			}

			if (control) break;
		}
	}
}

void commit()
{
	if (COMMIT.inputFlag == false) return;
	Instruction ins;
	int m;

	if (ROB.size() > 0)
	{
		ins = ROB.front().instruction;

		// IF ROB head is not store
		if (strcmp(ins.opcode, "STORE"))
		{
			if (ROB.size() < 2) m = ROB.size();
			else m = 2;

			for (int i = 0; i < m; i++)
			{
				ins = ROB.front().instruction;;

				if (ROB.front().readyToCommit)
				{
					if (strcmp(ins.opcode, "LOAD"))
					{
						commit_vector.push_back(ins);

						if (ins.pDesRegNum != -2)
						{registerArray[ins.destRegister].value = physicalRF[ins.pDesRegNum].value;}
					
						ROB.erase(ROB.begin());
					} 

					if (ins.branchTaken)
					{
						ROB.clear();
						break;
					}
				}
			}
		}

		if (m != 1 && ROB.size() > 0)
		{
			ins = ROB.front().instruction;

			if (!strcmp(ins.opcode, "STORE"))
			{
				if (ROB.front().readyToCommit && 
					LSQ.size() > 0)
				{
					char * robHead = ROB.front().instruction.rawData;
					char * lsqHead = LSQ.front().rawData;

					if (!strcmp(robHead, lsqHead) && !strcmp(MEM.inputI.rawData, ""))
					{
						MEM.inputI = LSQ.front();
						LSQ.erase(LSQ.begin()); 
					}
				}
			}
		}
	}

	if (LSQ.size() > 0)
	{
		ins = LSQ.front();

		if (!strcmp(ins.opcode, "LOAD") && 
			!strcmp(MEM.inputI.rawData, ""))
		{
			for (int i = 0; i < ROB.size(); i++)
			{
				if (!strcmp(ROB[i].instruction.rawData, ins.rawData))
				{ROB[i].readyToCommit = true;}
			}

			MEM.inputI = ins;
			LSQ.erase(LSQ.begin());
		}
	}
}

void mem()
{
	static int mem_counter = 0;	

	if (strcmp(MEM.inputI.rawData, ""))
	{
		//printf("mem counter: %d\n", mem_counter);

		if ((!strcmp(MEM.inputI.opcode, "STORE") && mem_counter == 2) ||
			(!strcmp(MEM.inputI.opcode, "LOAD") && mem_counter == 3))
		{
			if (!strcmp(MEM.inputI.opcode, "STORE")) 
			{
				commit_vector.push_back(MEM.inputI);
				ROB.erase(ROB.begin());
				dataArray[MEM.inputI.adressForMemory] = MEM.inputI.pSrcRegValue[0];
			}

			else if (!strcmp(MEM.inputI.opcode, "LOAD")) 
			{
				Instruction ins = MEM.inputI;

				commit_vector.push_back(ins);
				registerArray[ins.destRegister].value = dataArray[ins.adressForMemory];
				physicalRF[ins.pDesRegNum].value = dataArray[ins.adressForMemory];

				ROB.erase(ROB.begin());
			}

			strcpy(MEM.inputI.rawData, "");
			mem_counter = 0;
		}

		mem_counter++;
	}
}

void searchIQtoForward (int pDesReg, int writeThis)
{
	char * raw, * target;

	for (int i = 0; i < IQ.size(); i++)
	{
		//printf("IQ has data: %s", IQ[i].rawData);

		for (int j = 0; j < 2; j++)
		{
			//printf("inst: %s pSrcNum: %d, pDes: %d\n", IQ[i].opcode, 
			//							IQ[i].pSrcRegNum[j], pDesReg);

			if (IQ[i].pSrcRegNum[j] == pDesReg)
			{
				target = IQ[i].rawData;

				for (int k = 0; k < LSQ.size(); k++)
				{
					raw = LSQ[k].rawData;

					if (!strcmp(raw, target)) LSQ[k].pSrcRegValue[j] = writeThis;
				}

				IQ[i].pSrcRegValue[j] = writeThis;
				IQ[i].pSrcRegHasData[j] = true;
			}
		}
	}
}

void forwardToIQ()
{
	if (!strcmp(DU4.inputI.opcode, "DIV")) 
	{searchIQtoForward(DU4.inputI.pDesRegNum, DU4.inputI.dataToBeWritten);}

	if (!strcmp(MUL2.inputI.opcode, "MUL")) 
	{searchIQtoForward(MUL2.inputI.pDesRegNum, MUL2.inputI.dataToBeWritten);} 

	if (!strcmp(INT_FU.inputI.opcode, "ADD") ||
		!strcmp(INT_FU.inputI.opcode, "SUB") ||
		!strcmp(INT_FU.inputI.opcode, "EXOR") ||
		!strcmp(INT_FU.inputI.opcode, "OR") ||
		!strcmp(INT_FU.inputI.opcode, "AND"))
	{searchIQtoForward(INT_FU.inputI.pDesRegNum, INT_FU.inputI.dataToBeWritten);}

	else if (!strcmp(INT_FU.inputI.opcode, "MOVC")) 
	{searchIQtoForward(INT_FU.inputI.pDesRegNum, INT_FU.inputI.literal);}

	else if (!strcmp(INT_FU.inputI.opcode, "JAL")) 
	{searchIQtoForward(INT_FU.inputI.pDesRegNum, physicalRF[INT_FU.inputI.pDesRegNum].value);}

	else if (!strcmp(INT_FU.inputI.opcode, "STORE") ||
			 !strcmp(INT_FU.inputI.opcode, "LOAD"))
	{
		for (int i = 0; i < LSQ.size(); i++)
		{
			if (!strcmp(LSQ[i].rawData, INT_FU.inputI.rawData))
			{LSQ[i].adressForMemory = INT_FU.inputI.adressForMemory;}
		}
	}
}

// Generates empty instructions for the 
// stage given by parameter
void generateNOP(int which_stage)
{
	Instruction * nop = (Instruction *) malloc(sizeof(Instruction));
	nop -> pc = -2;
	strcpy(nop -> rawData, " - ");
	strcpy(nop -> opcode, " - ");
	nop -> destRegister = -2;
	nop -> srcRegisters[0] = -2;
	nop -> srcRegisters[1] = -2;

	nop -> pSrcRegValue[0] = -2;
	nop -> pSrcRegValue[1] = -2;
	nop -> pDesRegValue = -2;

	nop -> pSrcRegNum[0] = -2;
	nop -> pSrcRegNum[1] = -2;
	nop -> pDesRegNum = -2;

	if (which_stage == 0) Fetch.inputI = * nop;
	else if (which_stage == 1) Decode.inputI = * nop;
	else if (which_stage == 2) INT_FU.inputI = * nop;
	else if (which_stage == 3) MUL1.inputI = * nop;
	else if (which_stage == 4) MUL2.inputI = * nop;
	else if (which_stage == 5) DU1.inputI = * nop;
	else if (which_stage == 6) DU2.inputI = * nop;
	else if (which_stage == 7) DU3.inputI = * nop;
	else if (which_stage == 8) DU4.inputI = * nop;
}

// Flushes the pipeline if there is a halt, bz, 
// bnz or jump is encountered
void flush()
{
	// If the flushed instruction changed a register as invalid or PSW as busy
	// we should cancel that changes.
	int p = Decode.inputI.pDesRegNum;

	if (p != -2) physicalRF[p].valid--;
	
	PSWRegister.busy = 0;
	PSWRegister.zero = 0;

	IQ.clear();

	HALT_Flag = 0;

	generateNOP(0);
	generateNOP(1);

	PC = new_PC;
	NOP_Flag = false;
}

void print()
{
	const char ch = '\n';

	const char *stageNames[10] = {"Fetch   ", "DRF     ", "INT_FU  ", "MUL1    ", "MUL2    ", "DIV1    ", 
								  "DIV2    ", "DIV3    ", "DIV4    ", "MEM     "};
	struct Stage stages[10] = {Fetch, Decode, INT_FU, MUL1, MUL2, DU1, DU2, DU3, DU4, MEM};

	printf("---------------------------------\n");
	for (int i = 0; i < 10; i++)
	{
		if (strcmp(stages[i].inputI.rawData, "") == 0 ||
			strcmp(stages[i].inputI.rawData, " - ") == 0) printf("%s:   -\n", stageNames[i]);
		else
		{
			if (strchr(stages[i].inputI.rawData, ch)) printf("%s:  (pD:%d %d, pS1:%d %d, pS2:%d %d)  (I%d)  %s",
													 	stageNames[i],
													 	stages[i].inputI.pDesRegNum,
													 	stages[i].inputI.pDesRegValue,
													 	stages[i].inputI.pSrcRegNum[0],
													 	stages[i].inputI.pSrcRegValue[0],
													 	stages[i].inputI.pSrcRegNum[1],
													 	stages[i].inputI.pSrcRegValue[1],
													 	stages[i].inputI.pc / DATA_SIZE, 
													 	stages[i].inputI.rawData);
			else printf("%s:  (pD:%d %d, pS1:%d %d, pS2:%d %d)  (I%d)  %s\n",
													 	stageNames[i],
													 	stages[i].inputI.pDesRegNum,
													 	stages[i].inputI.pDesRegValue,
													 	stages[i].inputI.pSrcRegNum[0],
													 	stages[i].inputI.pSrcRegValue[0],
													 	stages[i].inputI.pSrcRegNum[1],
													 	stages[i].inputI.pSrcRegValue[1],
													 	stages[i].inputI.pc / DATA_SIZE, 
													 	stages[i].inputI.rawData);				
		}
	}
	printf("---------------------------------\n");

	printf("\nRename Table -->");
	printf("\n---------------------------------\n");
	for (int i = 0; i < PRF_SIZE; i++) 
	{
		if (physicalRF[i].hasData)
			printf("pReg[%d]: data: %d cor_arch: %d  valid: %d holdsDes: %d\n", 
											  i, physicalRF[i].value, 
											  physicalRF[i].correspondingArchReg,
											  physicalRF[i].valid,
											  physicalRF[i].holdsDes);
	}
	printf("---------------------------------\n");

	printf("\nIQ -->\n");
	printf("---------------------------------\n");
	for (int i = 0; i < IQ.size(); i++) 
	{
		if (strchr(IQ[i].rawData, ch))
			printf("IQ[%d]:  (pD:%d %d, pS1:%d %d, pS2:%d %d)  (I%d)  %s",
													 	i,
													 	IQ[i].pDesRegNum,
													 	IQ[i].pDesRegValue,
													 	IQ[i].pSrcRegNum[0],
													 	IQ[i].pSrcRegValue[0],
													 	IQ[i].pSrcRegNum[1],
													 	IQ[i].pSrcRegValue[1],
													 	IQ[i].pc / DATA_SIZE, 
													 	IQ[i].rawData);
		else printf("IQ[%d]:  (pD:%d %d, pS1:%d %d, pS2:%d %d)  (I%d)  %s\n",
													 	i,
													 	IQ[i].pDesRegNum,
													 	IQ[i].pDesRegValue,
													 	IQ[i].pSrcRegNum[0],
													 	IQ[i].pSrcRegValue[0],
													 	IQ[i].pSrcRegNum[1],
													 	IQ[i].pSrcRegValue[1],
													 	IQ[i].pc / DATA_SIZE, 
													 	IQ[i].rawData);
	}
	printf("---------------------------------");

	printf("\n\nROB -->\n");
	printf("---------------------------------\n");
	for (int i = 0; i < ROB.size(); i++)
	{
		Instruction ins = ROB[i].instruction;

		if (strchr(ins.rawData, ch))
			printf("ROB[%d]:  (pD:%d %d, pS1:%d %d, pS2:%d %d)  (I%d)  %s",
													 	i,
													 	ins.pDesRegNum,
													 	ins.pDesRegValue,
													 	ins.pSrcRegNum[0],
													 	ins.pSrcRegValue[0],
													 	ins.pSrcRegNum[1],
													 	ins.pSrcRegValue[1],
													 	ins.pc / DATA_SIZE, 
													 	ins.rawData);
		else printf("ROB[%d]:  (pD:%d %d, pS1:%d %d, pS2:%d %d)  (I%d)  %s\n",
													 	i,
													 	ins.pDesRegNum,
													 	ins.pDesRegValue,
													 	ins.pSrcRegNum[0],
													 	ins.pSrcRegValue[0],
													 	ins.pSrcRegNum[1],
													 	ins.pSrcRegValue[1],
													 	ins.pc / DATA_SIZE, 
													 	ins.rawData);
	}
	printf("---------------------------------\n");

	printf("\nCommit -->\n");
	printf("---------------------------------\n");
	for (int i = 0; i < commit_vector.size(); i++)
	{
		if (strchr(commit_vector[i].rawData, ch)) printf("Commited: (I%d) %s", 
												commit_vector[i].pc / DATA_SIZE, 
									 				commit_vector[i].rawData);
		else printf("Commited: (I%d) %s\n", 
				commit_vector[i].pc / DATA_SIZE, 
					  commit_vector[i].rawData);

		if (commit_vector[i].pDesRegNum != -2)
		{
			physicalRF[commit_vector[i].pDesRegNum].hasData = false;
			physicalRF[commit_vector[i].pDesRegNum].holdsDes = false;
			//printf("PRF index %d reclaimed\n", commit_vector[i].pDesRegNum);
		}
	}
	printf("---------------------------------\n");
	commit_vector.clear();

	printf("\nLSQ -->\n");
	printf("---------------------------------\n");
	for (int i = 0; i < LSQ.size(); i++)
	{
		if (strchr(LSQ[i].rawData, ch)) printf("LSQ[%d]: (I%d) %s", i, 
										LSQ[i].pc / DATA_SIZE, 
									   LSQ[i].rawData);
		else printf("LSQ[%d]: (I%d) %s\n", i, 
								LSQ[i].pc / DATA_SIZE, 
								LSQ[i].rawData);
	}
	printf("---------------------------------\n\n");

	printf("Registers:\n---------------------------------\n");
	for (int i = 0; i < RF_SIZE; i++)
	{
		printf(" ");
		if (i < 10) printf("Register  ");
		else printf("Register ");

		if (registerArray[i].valid > 0) printf("%d --> %d, invalid\n", i, registerArray[i].value);
		else printf("%d --> %d,  valid\n", i, registerArray[i].value);
	}

	printf("\nPSW: BUSY  ZERO CARRY NEGATIVE\n");
	char * tag;

	if (PSWRegister.busy > 0) printf("PSW: busy");
	else printf("PSW: valid");

	if (PSWRegister.zero == 1) printf(" true");
	else printf(" false");

	if (PSWRegister.carry == 0) printf(" true");
	else printf(" false");

	if (PSWRegister.negative == 0) printf("  true");
	else printf("  false");
	printf("\n---------------------------------\n");

	printf("\nData Segment:\n---------------------------------\n");
	for (int i = 0; i < 100; i++)
	{
		printf("  %d  ", dataArray[i]);
		if ((i + 1) % 5 == 0) printf("\n");
		else printf("--");
	}
	printf("---------------------------------\n");		
}