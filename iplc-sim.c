/***********************************************************************/
/***********************************************************************
 Pipeline Cache Simulator Solution
 ***********************************************************************/
/***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#define MAX_CACHE_SIZE 10240 
#define CACHE_MISS_DELAY 10 // 10 cycle cache miss penalty
#define MAX_STAGES 5

// init the simulator
void iplc_sim_init( int index, int blocksize, int assoc );

// Cache simulator functions
void iplc_sim_LRU_replace_on_miss( int index, int tag );
void iplc_sim_LRU_update_on_hit( int index, int assoc );
int iplc_sim_trap_address( unsigned int address );

// Pipeline functions
unsigned int iplc_sim_parse_reg( char *reg_str );
void iplc_sim_parse_instruction( char *buffer );
void iplc_sim_push_pipeline_stage();
void iplc_sim_process_pipeline_rtype( char *instruction, int dest_reg, 
				      int reg1, int reg2_or_constant);
void iplc_sim_process_pipeline_lw( int dest_reg, int base_reg, unsigned int data_address);
void iplc_sim_process_pipeline_sw( int src_reg, int base_reg, unsigned int data_address);
void iplc_sim_process_pipeline_branch( int reg1, int reg2);
void iplc_sim_process_pipeline_jump( );
void iplc_sim_process_pipeline_syscall( );
void iplc_sim_process_pipeline_nop( );

// Outout performance results
void iplc_sim_finalize();

typedef struct associativity
   {
   int vb; /* valid bit */
   int tag;
   } assoc_t;

typedef struct cache_line
   {
   assoc_t *assoc;
   int     *replacement;
   } cache_line_t;

cache_line_t *cache=NULL;
int cache_index=0; 
int cache_blocksize=0; 
int cache_blockoffsetbits = 0; 
int cache_assoc=0;
long cache_miss=0;
long cache_access=0;
long cache_hit=0;

char instruction[16];
char reg1[16];
char reg2[16];
char offsetwithreg[16];
unsigned int data_address=0;
unsigned int instruction_address=0;
unsigned int pipeline_cycles=0;   // how many cycles did you pipeline consume
unsigned int instruction_count=0; // home many real instructions ran thru the pipeline
unsigned int branch_predict_taken=0;
unsigned int branch_count=0;
unsigned int correct_branch_predictions=0;

unsigned int debug=1;
unsigned int dump_pipeline=1;

enum instruction_type {NOP, RTYPE, LW, SW, BRANCH, JUMP, JAL, SYSCALL};

typedef struct rtype
{
  char instruction[16];
  int reg1;
  int reg2_or_constant;
  int dest_reg;
  
} rtype_t;

typedef struct load_word
{
  unsigned int data_address;
  int dest_reg;
  int base_reg;

} lw_t;

typedef struct store_word
{
  unsigned int data_address;
  int src_reg;
  int base_reg;
} sw_t;

typedef struct branch
{
  int reg1;
  int reg2;

} branch_t;


typedef struct jump
{
  char instruction[16];
  
} jump_t;

typedef struct pipeline
{
  enum instruction_type itype;
  unsigned int instruction_address;
  union
  {
    rtype_t   rtype;
    lw_t      lw;
    sw_t      sw;
    branch_t  branch;
    jump_t    jump;
  } 
    stage;
  
} pipeline_t;

enum pipeline_stages {FETCH, DECODE, ALU, MEM, WRITEBACK};

pipeline_t pipeline[MAX_STAGES];

/************************************************************************************************/
/* Cache Functions ******************************************************************************/
/************************************************************************************************/

void iplc_sim_init( int index, int blocksize, int assoc )
   {
   int i=0, j=0;
   unsigned long cache_size = 0;
   cache_index = index;
   cache_blocksize = blocksize;
   cache_assoc = assoc;


   cache_blockoffsetbits = 
   (int) rint((log( (double) (blocksize * 4) )/ log(2)) );
   /* Note: rint function rounds the result up prior to casting */

   cache_size = assoc * ( 1 << index ) * ((32 * blocksize) + 33 - index - cache_blockoffsetbits);

   printf("Cache Configuration \n");
   printf("   Index: %d bits or %d lines \n", cache_index, (1<<cache_index) );
   printf("   BlockSize: %d \n", cache_blocksize );
   printf("   Associativity: %d \n", cache_assoc );
   printf("   BlockOffSetBits: %d \n", cache_blockoffsetbits );
   printf("   CacheSize: %lu \n", cache_size );
  
   if( cache_size > MAX_CACHE_SIZE )
     {
       printf("Cache too big. Great than MAX SIZE of %d .... \n", MAX_CACHE_SIZE);
        exit(-1);
     } 

   cache = (cache_line_t *) malloc( (sizeof(cache_line_t) * 1<<index) );
   
   for( i = 0; i < (1<<index); i++ )
      {
      cache[i].assoc = (assoc_t *)malloc( (sizeof(assoc_t) * assoc ) );
      cache[i].replacement = (int *)malloc( (sizeof(int) * assoc ) );

      for( j = 0; j < assoc; j++ )
	 {
         cache[i].assoc[j].vb = 0;
         cache[i].assoc[j].tag = 0;
         cache[i].replacement[j] = j;
	 }
      }

   // init the pipeline -- set all data to zero and instructions to NOP
   for( i = 0; i < MAX_STAGES; i++ )
     {
       // itype is set to O which is NOP type instruction
       bzero( &(pipeline[i]), sizeof(pipeline_t) );
     }
   }

void iplc_sim_LRU_replace_on_miss( int index, int tag )
   {
   int i=0, j=0;
	//Cycle through every cache and shift it down 1, (unless it is zero then save the index)
    for (i = 0; i < cache_assoc; i++) {
        if(cache[index].replacement[i] == 0){
		//Save index
		j = i;
	}
	else{
		//Shift down to chop-block
		cache[index].replacement[i]--;
	}
    }
    //Change value at replacement to be the "newest" value
    cache[index].replacement[j] = cache_assoc-1;
    cache[index].assoc[j].tag = tag; 
    cache[index].assoc[j].vb = 1;
   }

void iplc_sim_LRU_update_on_hit( int index, int assoc )
   {
   	int i=0, j=0;
	//assign j to saved value at hit
	j = cache[index].replacement[assoc];
	for(i =0; i < cache_assoc; i++){
		//condition to shift if larger than found value
     		if ( cache[index].replacement[i] > j){
			//shift it down
          		cache[index].replacement[i]--;
     		}
	}
        //Make the hit slot the "newest" cache slot
    	cache[index].replacement[assoc] = cache_assoc-1;
   } 

int iplc_sim_trap_address( unsigned int address )
   {
   int i=0, index=0;
   int tag=0;
   int hit=0;

   //Find tag by bitwise shifting block offset and index bits
   //Find index by removing block offset bits. Then use modulus 2^n where n is the amount of bits of index to isolate index bits
   cache_access += 1;
   index = (address >> cache_blockoffsetbits) % (1 << cache_index);
   tag = address >> (cache_blockoffsetbits + cache_index);
   printf("Address %x: Tag= %x, Index= %d \n", address, tag, index);
	
   //For each level of associativity, check the tag and valid bit to see if the tag found is in cache and valid
   for (i=0; i < cache_assoc; i++){
     if(cache[index].assoc[i].tag == tag && cache[index].assoc[i].vb){
       hit++;
       cache_hit++;
       printf("Addr %x HIT \n", address);

       //If not directly mapped, update the replacement array
       if (cache_assoc > 1) {
         iplc_sim_LRU_update_on_hit(index, i);
       }
       break;
     }
   }
   //If we miss, if not directly mapped, run the LRU miss function. If D.M., change out tag and vb
   if (hit == 0){
     cache_miss++;
     printf("Addr %x MISS \n", address);
     if (cache_assoc > 1) {
       iplc_sim_LRU_replace_on_miss(index, tag);
     }
     else{
       cache[index].assoc[0].tag = tag;
       cache[index].assoc[0].vb = 1;
     }
   }

   /* expects you to return 1 for hit, 0 for miss */
   return( hit );
}

void iplc_sim_finalize()
   {
     /* Finish processing all instructions in the Pipeline */
     while( pipeline[FETCH].itype != NOP ||
	    pipeline[DECODE].itype != NOP ||
	    pipeline[ALU].itype != NOP ||
	    pipeline[MEM].itype != NOP ||
	    pipeline[WRITEBACK].itype != NOP )
       {
	 iplc_sim_push_pipeline_stage();
       }

     printf(" Cache Performance \n");
     printf("\t Number of Cache Accesses is %ld \n", cache_access );
     printf("\t Number of Cache Misses is %ld \n", cache_miss );
     printf("\t Number of Cache Hits is %ld \n", cache_hit );
     printf("\t Cache Miss Rate is %f \n\n", (double)cache_miss / (double)cache_access );
     printf("Pipeline Performance \n");
     printf("\t Total Cycles is %u \n", pipeline_cycles );
     printf("\t Total Instructions is %u \n", instruction_count );
     printf("\t Total Branch Instructions is %u \n", branch_count );
     printf("\t Total Correct Branch Predictions is %u \n", correct_branch_predictions );
     printf("\t CPI is %f \n\n", (double)pipeline_cycles / (double)instruction_count );
   }

/************************************************************************************************/
/* Pipeline Functions ***************************************************************************/
/************************************************************************************************/

void iplc_sim_dump_pipeline()
{
  int i;

  for( i = 0; i < MAX_STAGES; i++ )
    {
      switch(i)
	{
	case FETCH:
	  printf("(cyc: %u) FETCH:\t %d: 0x%x \t", pipeline_cycles, pipeline[i].itype, pipeline[i].instruction_address);
	  break;
	case DECODE:
	  printf("DECODE:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
	  break;
	case ALU:
	  printf("ALU:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
	  break;
	case MEM:
	  printf("MEM:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
	  break;
	case WRITEBACK:
	  printf("WB:\t %d: 0x%x \n", pipeline[i].itype, pipeline[i].instruction_address);
	  break;
	default:
	  printf("DUMP: Bad stage!\n" );
	  exit(-1);
	}
    }
}

void iplc_sim_push_pipeline_stage()
{
  int i;
  int data_hit=1;
  int branch_taken=branch_predict_taken;
  int inserted_nop=0;

  /* 1. Count WRITEBACK stage is "retired" -- This I'm giving you */
  if( pipeline[WRITEBACK].instruction_address )
    {
      instruction_count++;
      if( debug )
	printf("DEBUG: Retired Instruction at 0x%x, Type %d, at Time %u \n", 
	       pipeline[WRITEBACK].instruction_address, pipeline[WRITEBACK].itype, pipeline_cycles );
    }

  /* 2. Check for BRANCH and correct/incorrect Branch Prediction */
  if (pipeline[DECODE].itype == BRANCH) {
	branch_count += 1;
	if( pipeline[FETCH].instruction_address ) {
		//Is the branch taken in this scenario?
	    if( pipeline[DECODE].instruction_address + 4 != pipeline[FETCH].instruction_address ) {
		    if( debug ) {
				printf("DEBUG: Branch Taken: FETCH addr = 0x%x, DECODE instr addr = 0x%x \n", 
					   pipeline[FETCH].instruction_address, pipeline[DECODE].instruction_address );
			}//Did we predict correctly?
			if( branch_taken ) {
				//We predicted correctly
				correct_branch_predictions += 1;
			} else {
				//We predicted incorrectly
				inserted_nop = 1;
			}
		} else {
			//The branch was not taken, did we predict correctly?
			if( !branch_taken ) {
				//We predicted correctly
				correct_branch_predictions += 1;
			} else {
				//We predicted incorrectly
				inserted_nop = 1;
			}
		}
	}
	//Shifting Pipeline when nop is inserted
	if( inserted_nop ) {
		pipeline[WRITEBACK] = pipeline[MEM];
  		pipeline[MEM] = pipeline[ALU];
  		pipeline[ALU] = pipeline[DECODE];
 		pipeline[DECODE].itype = NOP;
		pipeline[FETCH].instruction_address = 0;
		//Update Cycle Count
		pipeline_cycles++;
		//Retire Writeback
		if( pipeline[WRITEBACK].instruction_address ){
		      instruction_count++;
		      if( debug ) {
			  	  printf("DEBUG: Retired Instruction at 0x%x, Type %d, at Time %u \n", 
			       pipeline[WRITEBACK].instruction_address, pipeline[WRITEBACK].itype, pipeline_cycles );
			  }
		}
	}
	//Reset insert nop to keep #3 values pure
	inserted_nop = 0;
  }
	
  /* 3. Check for LW delays due to use in ALU stage and if data hit/miss  
   *    add delay cycles if needed.
   */
  if (pipeline[MEM].itype == LW){
      //checking whether data hit or not
      data_hit = iplc_sim_trap_address(pipeline[MEM].stage.lw.data_address);
      if(!data_hit){
        printf("DATA MISS:\t Address 0x%x \n", pipeline[MEM].stage.lw.data_address);
	//add delay cycles when missed and subtract 1 for instruction
        pipeline_cycles += CACHE_MISS_DELAY;
        pipeline_cycles -= 1;
      }
      else{
        printf("DATA HIT:\t Address 0x%x \n", pipeline[MEM].stage.lw.data_address);
      }
      //checking if due to ALU stage
      if(pipeline[ALU].itype == RTYPE && 
	   (pipeline[ALU].stage.rtype.reg1 == pipeline[MEM].stage.lw.dest_reg || pipeline[ALU].stage.rtype.reg2_or_constant == pipeline[MEM].stage.lw.dest_reg)){
        inserted_nop = 1;
	//Update Cycle Count
        pipeline_cycles++;
	//Insert the nop
	pipeline[WRITEBACK] = pipeline[MEM];
	pipeline[MEM].instruction_address = 0;
	//Retire Writeback
	if( pipeline[WRITEBACK].instruction_address ){
		     instruction_count++;
		     if( debug ) {
		             printf("DEBUG: Retired Instruction at 0x%x, Type %d, at Time %u \n", 
			       pipeline[WRITEBACK].instruction_address, pipeline[WRITEBACK].itype, pipeline_cycles );
		     }
	}
      }
      //when data isn't hit and inserted nop is true add delay cycle
      if(data_hit == 0 && inserted_nop == 1){
        pipeline_cycles -= 1;
      }
  }
  /* 4. Check for SW mem acess and data miss .. add delay cycles if needed */
    if (pipeline[MEM].itype == SW){
    //checking if data was hit or not
    data_hit = iplc_sim_trap_address(pipeline[MEM].stage.sw.data_address);
    if(!data_hit){
      printf("DATA MISS:\t Address 0x%x \n", pipeline[MEM].stage.sw.data_address);
      //add delay cycles when missed and subtract 1 for instruction
      pipeline_cycles += CACHE_MISS_DELAY;
      pipeline_cycles -= 1;
    }
    else{
      printf("DATA HIT:\t Address 0x%x \n", pipeline[MEM].stage.sw.data_address);
    }
  }
  /* 5. Increment pipe_cycles 1 cycle for normal processing */
  pipeline_cycles++;
	
  /* 6. push stages thru MEM->WB, ALU->MEM, DECODE->ALU, FETCH->DECODE */
  pipeline[WRITEBACK] = pipeline[MEM];
  pipeline[MEM] = pipeline[ALU];
  pipeline[ALU] = pipeline[DECODE];
  pipeline[DECODE] = pipeline[FETCH];
    	
  // 7. This is a give'me -- Reset the FETCH stage to NOP via bzero */
  bzero( &(pipeline[FETCH]), sizeof(pipeline_t) );
}

void iplc_sim_process_pipeline_rtype( char *instruction, int dest_reg, int reg1, int reg2_or_constant)
{
  /* this is an example of what you need to do for the rest */
  iplc_sim_push_pipeline_stage();

  pipeline[FETCH].itype = RTYPE;
  pipeline[FETCH].instruction_address = instruction_address;

  strcpy( pipeline[FETCH].stage.rtype.instruction, instruction );
  pipeline[FETCH].stage.rtype.reg1 = reg1;
  pipeline[FETCH].stage.rtype.reg2_or_constant = reg2_or_constant;
  pipeline[FETCH].stage.rtype.dest_reg = dest_reg;
}

void iplc_sim_process_pipeline_lw( int dest_reg, int base_reg, unsigned int data_address)
{
  iplc_sim_push_pipeline_stage();
  //setting itype to correct processing to match function
  pipeline[FETCH].itype = LW;
  pipeline[FETCH].instruction_address = instruction_address;
  //setting the variables for the struct from the parameters
  pipeline[FETCH].stage.lw.dest_reg = dest_reg;
  pipeline[FETCH].stage.lw.base_reg = base_reg;
  pipeline[FETCH].stage.lw.data_address = data_address;
}

void iplc_sim_process_pipeline_sw( int src_reg, int base_reg, unsigned int data_address)
{
  iplc_sim_push_pipeline_stage();
  //setting itype to correct processing to match function
  pipeline[FETCH].itype = SW;
  pipeline[FETCH].instruction_address = instruction_address;
  //setting the variables for the struct from the parameters
  pipeline[FETCH].stage.sw.src_reg = src_reg;
  pipeline[FETCH].stage.sw.base_reg = base_reg;
  pipeline[FETCH].stage.sw.data_address = data_address;
}

void iplc_sim_process_pipeline_branch( int reg1, int reg2)
{
  iplc_sim_push_pipeline_stage();
  //setting itype to correct processing to match function
  pipeline[FETCH].itype = BRANCH;
  pipeline[FETCH].instruction_address = instruction_address;
  //setting the variables for the struct from the parameters
  pipeline[FETCH].stage.branch.reg1 = reg1;
  pipeline[FETCH].stage.branch.reg2 = reg2;
}

void iplc_sim_process_pipeline_jump( char* instruction )
{
  iplc_sim_push_pipeline_stage();
  //setting itype to correct processing to match function
  pipeline[FETCH].itype = JUMP;
  pipeline[FETCH].instruction_address = instruction_address;

  //setting the variables for the struct from the parameters
  strcpy(pipeline[FETCH].stage.jump.instruction, instruction);
}

void iplc_sim_process_pipeline_syscall( )
{
  iplc_sim_push_pipeline_stage();
  //setting itype to correct processing to match function
  pipeline[FETCH].itype = SYSCALL;
  pipeline[FETCH].instruction_address = instruction_address;
}

void iplc_sim_process_pipeline_nop( )
{
  iplc_sim_push_pipeline_stage();
  //setting itype to correct processing to match function
  pipeline[FETCH].itype = NOP;
  pipeline[FETCH].instruction_address = instruction_address;
}
/************************************************************************************************/
/* parse Function *******************************************************************************/
/************************************************************************************************/

unsigned int iplc_sim_parse_reg( char *reg_str )
{
  int i;
  // turn comma into \n
  if( reg_str[strlen(reg_str)-1] == ',' )
    reg_str[strlen(reg_str)-1] = '\n';

  if( reg_str[0] != '$' )
    return( atoi( reg_str ) );
  else
    {
      // copy down over $ character than return atoi
      for( i = 0; i < strlen(reg_str); i++ )
	reg_str[i] = reg_str[i+1];

      return( atoi( reg_str) );
    }
}

void iplc_sim_parse_instruction( char *buffer )
{
  int instruction_hit = 0;
  int i=0, j=0;
  int src_reg=0;
  int src_reg2=0;
  int dest_reg=0;
  char str_src_reg[16];
  char str_src_reg2[16];
  char str_dest_reg[16];
  char str_constant[16];

  if( sscanf(buffer, "%x %s", &instruction_address, instruction ) != 2 )
    {
      printf("Malformed instruction \n" );
      exit(-1);
    }

  instruction_hit = iplc_sim_trap_address( instruction_address );

  // if a MISS, then push current instruction thru pipeline
  if( !instruction_hit )
    {
      // need to subtract 1, since the stage is pushed once more for actual instruction processing
      // also need to allow for a branch miss prediction during the fetch cache miss time -- by
      // counting cycles this allows for these cycles to overlap and not doubly count.

      printf("INST MISS:\t Address 0x%x \n", instruction_address );      

      for( i = pipeline_cycles, j = pipeline_cycles; i < j + CACHE_MISS_DELAY - 1; i++ )
	iplc_sim_push_pipeline_stage();
    }
  else
    printf("INST HIT:\t Address 0x%x \n", instruction_address );      

  // Parse the Instruction

  if( strncmp( instruction, "add", 3 ) == 0 ||
      strncmp( instruction, "sll", 3 ) == 0 ||
      strncmp( instruction, "ori", 3 ) == 0  )
    {
      if( sscanf(buffer, "%x %s %s %s %s", 
		 &instruction_address, 
		 instruction, 
		 str_dest_reg, 
		 str_src_reg, 
		 str_src_reg2 ) != 5 )
	{
	  printf("Malformed RTYPE instruction (%s) at address 0x%x \n", 
		 instruction, instruction_address );
	  exit(-1);
	}

      dest_reg = iplc_sim_parse_reg( str_dest_reg );
      src_reg = iplc_sim_parse_reg( str_src_reg );
      src_reg2 = iplc_sim_parse_reg( str_src_reg2 );

      iplc_sim_process_pipeline_rtype( instruction, dest_reg, src_reg, src_reg2 );
    } 

  else if( strncmp( instruction, "lui", 3 ) == 0 )
    {
      if( sscanf(buffer, "%x %s %s %s", 
		 &instruction_address, 
		 instruction, 
		 str_dest_reg, 
		 str_constant ) != 4 )
	{
	  printf("Malformed RTYPE instruction (%s) at address 0x%x \n", 
		 instruction, instruction_address );
	  exit(-1);
	}

      dest_reg = iplc_sim_parse_reg( str_dest_reg );
      src_reg = -1;
      src_reg2 = -1;
      iplc_sim_process_pipeline_rtype( instruction, dest_reg, src_reg, src_reg2 );
    }

  else if( strncmp( instruction, "lw", 2 ) == 0 ||
	   strncmp( instruction, "sw", 2 ) == 0  )
    {
      if( sscanf( buffer, "%x %s %s %s %x", 
		  &instruction_address, 
		  instruction, 
		  reg1, 
		  offsetwithreg, 
		  &data_address ) != 5)
	{
	  printf("Bad instruction: %s at address %x \n", instruction, instruction_address);
	  exit(-1);
	}

      if( strncmp( instruction, "lw", 2 ) == 0)
	{

	  dest_reg = iplc_sim_parse_reg( reg1 );

	  // don't need to worry about base regs -- just insert -1 values
	  iplc_sim_process_pipeline_lw( dest_reg, -1, data_address);
	}
      if( strncmp( instruction, "sw", 2 ) == 0)
	{
	  src_reg = iplc_sim_parse_reg( reg1 );

	  // don't need to worry about base regs -- just insert -1 values
	  iplc_sim_process_pipeline_sw( src_reg, -1, data_address);
	}
    }
  else if( strncmp( instruction, "beq", 3 ) == 0 )
    {
      // don't need to worry about getting regs -- just insert -1 values
      iplc_sim_process_pipeline_branch(-1, -1);
    }
  else if( strncmp( instruction, "jal", 3 ) == 0 ||
	   strncmp( instruction, "jr", 2 ) == 0 ||
	   strncmp( instruction, "j", 1 ) == 0 )
    {
      iplc_sim_process_pipeline_jump( instruction );
    }
  else if( strncmp( instruction, "jal", 3 ) == 0 ||
	   strncmp( instruction, "jr", 2 ) == 0 ||
	   strncmp( instruction, "j", 1 ) == 0 )
    {
      /*
       * Note: no need to worry about forwarding on the jump register
       * we'll let that one go.
       */
      iplc_sim_process_pipeline_jump( instruction );
    }
  else if( strncmp( instruction, "syscall", 7 ) == 0)
    {
      iplc_sim_process_pipeline_syscall( );
    }
  else if( strncmp( instruction, "nop", 3 ) == 0)
    {
      iplc_sim_process_pipeline_nop( );
    }
  else
    {
      printf("Do not know how to process instruction: %s at address %x \n",
	     instruction, instruction_address );
      exit(-1);
    }
}

/************************************************************************************************/
/* MAIN Function ********************************************************************************/
/************************************************************************************************/

int main()
{
  char trace_file_name[1024];
  FILE *trace_file = NULL;
  char buffer[80];
  int index = 10;
  int blocksize = 1;
  int assoc = 1;

  printf("Please enter the tracefile: ");
  scanf("%s", trace_file_name );

  trace_file = fopen(trace_file_name, "r" );

  if( trace_file == NULL )
    {
      printf("fopen failed for %s file\n", trace_file_name);
      exit(-1);
    }

  printf("Enter Cache Size (index), Blocksize and Level of Assoc \n");
  scanf( "%d %d %d", &index, &blocksize, &assoc );

  printf("Enter Branch Prediction: 0 (NOT taken), 1 (TAKEN): ");
  scanf("%d", &branch_predict_taken );

  iplc_sim_init( index, blocksize, assoc );


  while( fgets( buffer, 80, trace_file ) != NULL )
    {
      iplc_sim_parse_instruction( buffer );
      if( dump_pipeline )
	iplc_sim_dump_pipeline();
    }

  iplc_sim_finalize();
  return 0;
}
