/*
 * CoreFreq
 * Copyright (C) 2015-2016 CYRIL INGENIERIE
 * Licenses: GPL2
 */

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/msr.h>

#include "coretypes.h"
#include "intelasm.h"
#include "intelmsr.h"
#include "corefreq-api.h"
#include "corefreqk.h"

MODULE_AUTHOR ("CYRIL INGENIERIE <labs[at]cyring[dot]fr>");
MODULE_DESCRIPTION ("CoreFreq Processor Driver");
MODULE_SUPPORTED_DEVICE ("Intel Core Core2 Atom Xeon i3 i5 i7");
MODULE_LICENSE ("GPL");

static struct
{
	signed int Major;
	struct cdev *kcdev;
	dev_t nmdev, mkdev;
	struct class *clsdev;
} CoreFreqK;

static signed int ArchID=-1;
module_param(ArchID, int, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
MODULE_PARM_DESC(ArchID, "Force an Architecture ID");

static signed int AutoClock=1;
module_param(AutoClock, int, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
MODULE_PARM_DESC(AutoClock, "Auto Estimate Clock Frequency");

static PROC *Proc=NULL;
static KPUBLIC *KPublic=NULL;
static KPRIVATE *KPrivate=NULL;

static ARCH Arch[ARCHITECTURES]=
{
/*  0*/	{ _GenuineIntel,       Arch_Genuine,     NULL                         },

/*  1*/	{ _Core_Yonah,         Arch_Genuine,     "Core/Yonah"                 },
/*  2*/	{ _Core_Conroe,        Arch_Core2,       "Core2/Conroe"               },
/*  3*/	{ _Core_Kentsfield,    Arch_Core2,       "Core2/Kentsfield"           },
/*  4*/	{ _Core_Conroe_616,    Arch_Core2,       "Core2/Conroe/Yonah"         },
/*  5*/	{ _Core_Yorkfield,     Arch_Core2,       "Core2/Yorkfield"            },
/*  6*/	{ _Core_Dunnington,    Arch_Core2,       "Xeon/Dunnington"            },

/*  7*/	{ _Atom_Bonnell,       Arch_Core2,       "Atom/Bonnell"               },
/*  8*/	{ _Atom_Silvermont,    Arch_Core2,       "Atom/Silvermont"            },
/*  9*/	{ _Atom_Lincroft,      Arch_Core2,       "Atom/Lincroft"              },
/* 10*/	{ _Atom_Clovertrail,   Arch_Core2,       "Atom/Clovertrail"           },
/* 11*/	{ _Atom_Saltwell,      Arch_Core2,       "Atom/Saltwell"              },

/* 12*/	{ _Silvermont_637,     Arch_Nehalem,     "Silvermont"                 },
/* 13*/	{ _Silvermont_64D,     Arch_Nehalem,     "Silvermont"                 },

/* 14*/	{ _Atom_Airmont,       Arch_Core2,       "Atom/Airmont"               },
/* 15*/	{ _Atom_Goldmont,      Arch_Core2,       "Atom/Goldmont"              },
/* 16*/	{ _Atom_Sofia,         Arch_Core2,       "Atom/Sofia"                 },
/* 17*/	{ _Atom_Merrifield,    Arch_Core2,       "Atom/Merrifield"            },
/* 18*/	{ _Atom_Moorefield,    Arch_Core2,       "Atom/Moorefield"            },

/* 19*/	{ _Nehalem_Bloomfield, Arch_Nehalem,     "Nehalem/Bloomfield"         },
/* 20*/	{ _Nehalem_Lynnfield,  Arch_Nehalem,     "Nehalem/Lynnfield"          },
/* 21*/	{ _Nehalem_MB,         Arch_Nehalem,     "Nehalem/Mobile"             },
/* 22*/	{ _Nehalem_EX,         Arch_Nehalem,     "Nehalem/eXtreme.EP"         },

/* 23*/	{ _Westmere,           Arch_Nehalem,     "Westmere"                   },
/* 24*/	{ _Westmere_EP,        Arch_Nehalem,     "Westmere/EP"                },
/* 25*/	{ _Westmere_EX,        Arch_Nehalem,     "Westmere/eXtreme"           },

/* 26*/	{ _SandyBridge,        Arch_SandyBridge, "SandyBridge"                },
/* 27*/	{ _SandyBridge_EP,     Arch_SandyBridge, "SandyBridge/eXtreme.EP"     },

/* 28*/	{ _IvyBridge,          Arch_SandyBridge, "IvyBridge"                  },
/* 29*/	{ _IvyBridge_EP,       Arch_SandyBridge, "IvyBridge/EP"               },

/* 30*/	{ _Haswell_DT,         Arch_SandyBridge, "Haswell/Desktop"            },
/* 31*/	{ _Haswell_MB,         Arch_SandyBridge, "Haswell/Mobile"             },
/* 32*/	{ _Haswell_ULT,        Arch_SandyBridge, "Haswell/Ultra Low TDP"      },
/* 33*/	{ _Haswell_ULX,        Arch_SandyBridge, "Haswell/Ultra Low eXtreme"  },

/* 34*/	{ _Broadwell,          Arch_SandyBridge, "Broadwell/Mobile"           },
/* 35*/	{ _Broadwell_EP,       Arch_SandyBridge, "Broadwell/EP"               },
/* 36*/	{ _Broadwell_H,        Arch_SandyBridge, "Broadwell/H"                },
/* 37*/	{ _Broadwell_EX,       Arch_SandyBridge, "Broadwell/EX"               },

/* 38*/	{ _Skylake_UY,         Arch_SandyBridge, "Skylake/UY"                 },
/* 39*/	{ _Skylake_S,          Arch_SandyBridge, "Skylake/S"                  },
/* 40*/	{ _Skylake_E,          Arch_SandyBridge, "Skylake/E"                  }
};


unsigned int Core_Count(void)
{
	unsigned int count=0;

	asm volatile
	(
		"cpuid"
		: "=a"	(count)
		: "a"	(0x4),
		  "c"	(0x0)
	);
	count=(count >> 26) & 0x3f;
	count++;
	return(count);
}

unsigned int Proc_Brand(char *pBrand)
{
	char idString[64]={0x20};
	unsigned int ix=0, jx=0, px=0;
	unsigned int frequency=0, multiplier=0;
	BRAND Brand;

	for(ix=0; ix < 3; ix++)
	{
		asm volatile
		(
			"cpuid"
			: "=a"  (Brand.AX),
			  "=b"  (Brand.BX),
			  "=c"  (Brand.CX),
			  "=d"  (Brand.DX)
			: "a"   (0x80000002 + ix)
		);
		for(jx=0; jx < 4; jx++, px++)
			idString[px]=Brand.AX.Chr[jx];
		for(jx=0; jx < 4; jx++, px++)
			idString[px]=Brand.BX.Chr[jx];
		for(jx=0; jx < 4; jx++, px++)
			idString[px]=Brand.CX.Chr[jx];
		for(jx=0; jx < 4; jx++, px++)
			idString[px]=Brand.DX.Chr[jx];
	}
	for(ix=0; ix < 46; ix++)
		if((idString[ix+1] == 'H') && (idString[ix+2] == 'z'))
		{
			switch(idString[ix])
			{
				case 'M':
					multiplier=1;
				break;
				case 'G':
					multiplier=1000;
				break;
				case 'T':
					multiplier=1000000;
				break;
			}
			break;
		}
	if(multiplier > 0)
	{
	    if(idString[ix-3] == '.')
	    {
		frequency  = (int) (idString[ix-4] - '0') * multiplier;
		frequency += (int) (idString[ix-2] - '0') * (multiplier / 10);
		frequency += (int) (idString[ix-1] - '0') * (multiplier / 100);
	    }
	    else
	    {
		frequency  = (int) (idString[ix-4] - '0') * 1000;
		frequency += (int) (idString[ix-3] - '0') * 100;
		frequency += (int) (idString[ix-2] - '0') * 10;
		frequency += (int) (idString[ix-1] - '0');
		frequency *= frequency;
	    }
	}
	for(ix=jx=0; jx < 48; jx++)
		if(!(idString[jx] == 0x20 && idString[jx+1] == 0x20))
			pBrand[ix++]=idString[jx];

	return(frequency);
}

// Retreive the Processor features through calls to the CPUID instruction.
void Proc_Features(FEATURES *features)
{
	int BX=0, DX=0, CX=0;
	asm volatile
	(
		"cpuid"
		: "=a"	(features->LargestStdFunc),
		  "=b"	(BX),
		  "=d"	(DX),
		  "=c"	(CX)
                : "a" (0x0)
	);
	features->VendorID[ 0]=BX;
	features->VendorID[ 1]=(BX >> 8);
	features->VendorID[ 2]=(BX >> 16);
	features->VendorID[ 3]=(BX >> 24);
	features->VendorID[ 4]=DX;
	features->VendorID[ 5]=(DX >> 8);
	features->VendorID[ 6]=(DX >> 16);
	features->VendorID[ 7]=(DX >> 24);
	features->VendorID[ 8]=CX;
	features->VendorID[ 9]=(CX >> 8);
	features->VendorID[10]=(CX >> 16);
	features->VendorID[11]=(CX >> 24);

	asm volatile
	(
		"cpuid"
		: "=a"	(features->Std.AX),
		  "=b"	(features->Std.BX),
		  "=c"	(features->Std.CX),
		  "=d"	(features->Std.DX)
                : "a" (0x1)
	);
	asm volatile
	(
		"cpuid"
		: "=a"	(features->MONITOR_MWAIT_Leaf.AX),
		  "=b"	(features->MONITOR_MWAIT_Leaf.BX),
		  "=c"	(features->MONITOR_MWAIT_Leaf.CX),
		  "=d"	(features->MONITOR_MWAIT_Leaf.DX)
                : "a" (0x5)
	);
	asm volatile
	(
		"cpuid"
		: "=a"	(features->Thermal_Power_Leaf.AX),
		  "=b"	(features->Thermal_Power_Leaf.BX),
		  "=c"	(features->Thermal_Power_Leaf.CX),
		  "=d"	(features->Thermal_Power_Leaf.DX)
                : "a" (0x6)
	);
	asm volatile
	(
		"movq	$0x7, %%rax	\n\t"
		"xorq	%%rbx, %%rbx    \n\t"
		"xorq	%%rcx, %%rcx    \n\t"
		"xorq	%%rdx, %%rdx    \n\t"
		"cpuid			\n\t"
		"mov	%%eax, %0	\n\t"
		"mov	%%ebx, %1	\n\t"
		"mov	%%ecx, %2	\n\t"
		"mov	%%edx, %3"
		: "=r"	(features->ExtFeature.AX),
		  "=r"	(features->ExtFeature.BX),
		  "=r"	(features->ExtFeature.CX),
		  "=r"	(features->ExtFeature.DX)
                :
		: "%rax", "%rbx", "%rcx", "%rdx"
	);
	asm volatile
	(
		"cpuid"
		: "=a"	(features->Perf_Monitoring_Leaf.AX),
		  "=b"	(features->Perf_Monitoring_Leaf.BX),
		  "=c"	(features->Perf_Monitoring_Leaf.CX),
		  "=d"	(features->Perf_Monitoring_Leaf.DX)
                : "a" (0xa)
	);
	asm volatile
	(
		"cpuid"
		: "=a"	(features->LargestExtFunc)
                : "a" (0x80000000)
	);
	if(features->LargestExtFunc >= 0x80000004 \
	&& features->LargestExtFunc <= 0x80000008)
	{
		asm volatile
		(
			"cpuid"
			: "=d"	(features->InvariantTSC)
			: "a" (0x80000007)
		);
		features->InvariantTSC &= 0x100;
		features->InvariantTSC >>= 8;

		asm volatile
		(
			"cpuid"
			: "=c"	(features->ExtFunc.CX),
			  "=d"	(features->ExtFunc.DX)
			: "a" (0x80000001)
		);
	}
}

DECLARE_COMPLETION(bclk_job_complete);

typedef	struct {
	unsigned long long V[2];
} TSC_STRUCT;

#define	OCCURRENCES 8

signed int Compute_Clock(void *arg)
{
	CLOCK *clock=(CLOCK *) arg;
	unsigned int ratio=clock->Q;
	struct kmem_cache *hardwareCache=kmem_cache_create(
				"CoreFreqCache",
				OCCURRENCES * sizeof(TSC_STRUCT), 0,
				SLAB_HWCACHE_ALIGN, NULL);
	TSC_STRUCT *TSC[2]={
		kmem_cache_alloc(hardwareCache, GFP_KERNEL),
		kmem_cache_alloc(hardwareCache, GFP_KERNEL)
	};
	unsigned long long D[2][OCCURRENCES];
	unsigned int loop=0, what=0, best[2]={0, 0}, top[2]={0, 0};

	// No preemption, no interrupt.
	unsigned long flags;
	preempt_disable();
	raw_local_irq_save(flags);

	// Warm-up
	for(loop=0; loop < OCCURRENCES; loop++)
	{
		RDTSC64(TSC[0][loop].V[0]);
		RDTSC64(TSC[0][loop].V[1]);
	}
	// Pick-up
	for(loop=0; loop < OCCURRENCES; loop++)
	{
		RDTSC64(TSC[1][loop].V[0]);
		udelay(1000);
		RDTSC64(TSC[1][loop].V[1]);
	}
	// Restore preemption and interrupt.
	raw_local_irq_restore(flags);
	preempt_enable();

	memset(D, 0, 2 * OCCURRENCES);
	for(loop=0; loop < OCCURRENCES; loop++)
		for(what=0; what < 2; what++) {
			D[what][loop] 	= TSC[what][loop].V[1]
					- TSC[what][loop].V[0];
		}
	for(loop=0; loop < OCCURRENCES; loop++) {
		unsigned int inner=0, count[2]={0, 0};
		for(inner=loop; inner < OCCURRENCES; inner++) {
			for(what=0; what < 2; what++) {
				if(D[what][loop] == D[what][inner])
					count[what]++;
			}
		}
		for(what=0; what < 2; what++) {
			if((count[what] > top[what])
			||((count[what] == top[what])
			&& (D[what][loop] < D[what][best[what]]))) {
				top[what]=count[what];
				best[what]=loop;

			}
		}
	}
	D[1][best[1]] -= D[0][best[0]];
	D[1][best[1]] *= 1000;

	clock->Q=D[1][best[1]] / (1000000L * ratio);
	clock->R=D[1][best[1]] % (1000000L * ratio);

	clock->Hz=D[1][best[1]] / ratio;
	clock->Hz+=D[1][best[1]] % ratio;

	kmem_cache_free(hardwareCache, TSC[1]);
	kmem_cache_free(hardwareCache, TSC[0]);
	kmem_cache_destroy(hardwareCache);

	complete_and_exit(&bclk_job_complete, 0);
}

CLOCK Base_Clock(unsigned int ratio)
{
	CLOCK clock={.Q=ratio, .R=0};
	struct task_struct *tid=kthread_create(	Compute_Clock,
						&clock,
						"baseclock/%-3d",
						0);
	if(!IS_ERR(tid))
	{
		kthread_bind(tid, 0);
		wake_up_process(tid);
		wait_for_completion(&bclk_job_complete);
	}
	return(clock);
}

// [Genuine Intel]
CLOCK Clock_GenuineIntel(unsigned int ratio)
{
	CLOCK clock={.Q=100, .R=0};

	if(Proc->Features.FactoryFreq > 0)
	{
		clock.Q=Proc->Features.FactoryFreq / ratio;
		clock.R=(Proc->Features.FactoryFreq % ratio) * PRECISION;
	}
	return(clock);
};

// [Core]
CLOCK Clock_Core(unsigned int ratio)
{
	CLOCK clock={.Q=100, .R=0};
	FSB_FREQ FSB={.value=0};

	RDMSR(FSB, MSR_FSB_FREQ);
	switch(FSB.Bus_Speed)
	{
		case 0b101: {
			clock.Q=100;
			clock.R=0;
		};
		break;
		case 0b001: {
			clock.Q=133;
			clock.R=3333;
		}
		break;
		case 0b011: {
			clock.Q=166;
			clock.R=6666;
		}
		break;
	}
	clock.R *= ratio;
	return(clock);
};

// [Core2]
CLOCK Clock_Core2(unsigned int ratio)
{
	CLOCK clock={.Q=100, .R=0};
	FSB_FREQ FSB={.value=0};

	RDMSR(FSB, MSR_FSB_FREQ);
	switch(FSB.Bus_Speed)
	{
		case 0b101: {
			clock.Q=100;
			clock.R=0;
		}
		break;
		case 0b001: {
			clock.Q=133;
			clock.R=3333;
		}
		break;
		case 0b011: {
			clock.Q=166;
			clock.R=6666;
		}
		break;
		case 0b010: {
			clock.Q=200;
			clock.R=0;
		}
		break;
		case 0b000: {
			clock.Q=266;
			clock.R=6666;
		}
		break;
		case 0b100: {
			clock.Q=333;
			clock.R=3333;
		}
		break;
		case 0b110: {
			clock.Q=400;
			clock.R=0;
		}
		break;
	}
	clock.R *= ratio;
	return(clock);
};

// [Atom]
CLOCK Clock_Atom(unsigned int ratio)
{
	CLOCK clock={.Q=83, .R=0};
	FSB_FREQ FSB={.value=0};

	RDMSR(FSB, MSR_FSB_FREQ);
	switch(FSB.Bus_Speed)
	{
		case 0b111: {
			clock.Q=83;
			clock.R=2000;
		}
		break;
		case 0b101: {
			clock.Q=99;
			clock.R=8400;
		}
		break;
		case 0b001: {
			clock.Q=133;
			clock.R=2000;
		}
		break;
		case 0b011: {
			clock.Q=166;
			clock.R=4000;
		}
		break;
		default: {
			clock.Q=83;
			clock.R=2000;
		}
		break;
	}
	clock.R *= ratio;
	return(clock);
};

// [Silvermont]
CLOCK Clock_Silvermont(unsigned int ratio)
{
	CLOCK clock={.Q=83, .R=3};
	FSB_FREQ FSB={.value=0};

	RDMSR(FSB, MSR_FSB_FREQ);
	switch(FSB.Bus_Speed)
	{
		case 0b100: {
			clock.Q=80;
			clock.R=0;
		}
		break;
		case 0b000: {
			clock.Q=83;
			clock.R=3000;
		}
		break;
		case 0b001: {
			clock.Q=100;
			clock.R=0;
		}
		break;
		case 0b010: {
			clock.Q=133;
			clock.R=3333;
		}
		break;
		case 0b011: {
			clock.Q=116;
			clock.R=7000;
		}
		break;
	}
	clock.R *= ratio;
	return(clock);
};

// [Nehalem]
CLOCK Clock_Nehalem(unsigned int ratio)
{
	CLOCK clock={.Q=133, .R=3333};
	clock.R *= ratio;
	return(clock);
};

// [Westmere]
CLOCK Clock_Westmere(unsigned int ratio)
{
	CLOCK clock={.Q=133, .R=3333};
	clock.R *= ratio;
	return(clock);
};

// [SandyBridge]
CLOCK Clock_SandyBridge(unsigned int ratio)
{
	CLOCK clock={.Q=100, .R=0};
	clock.R *= ratio;
	return(clock);
};

// [IvyBridge]
CLOCK Clock_IvyBridge(unsigned int ratio)
{
	CLOCK clock={.Q=100, .R=0};
	clock.R *= ratio;
	return(clock);
};

// [Haswell]
CLOCK Clock_Haswell(unsigned int ratio)
{
	CLOCK clock={.Q=100, .R=0};
	clock.R *= ratio;
	return(clock);
};

void Cache_Topology(CORE *Core)
{
	unsigned int level=0x0;
	for(level=0; level < CACHE_MAX_LEVEL; level++)
	{
		asm volatile
		(
			"cpuid"
			: "=b"	(Core->T.Cache[level].Register),
			  "=c"	(Core->T.Cache[level].Sets)
			: "a"	(0x4),
			  "c"	(level)
		);
	}
}

DECLARE_COMPLETION(topology_job_complete);

// Enumerate the Processor's Cores and Threads topology.
signed int Map_Topology(void *arg)
{
	if(arg != NULL)
	{
		CORE *Core=(CORE *) arg;
		FEATURES features;

		RDMSR(Core->T.Base, MSR_IA32_APICBASE);

		asm volatile
		(
			"cpuid"
			: "=b"	(features.Std.BX)
			: "a"	(0x1)
		);
		Core->T.ApicID=features.Std.BX.Apic_ID;
		Core->T.CoreID=Core_Count();
		Core->T.ThreadID=features.Std.BX.MaxThread;
		Cache_Topology(Core);

		complete_and_exit(&topology_job_complete, 0);
	}
	else
		complete_and_exit(&topology_job_complete, -1);
}

signed int Map_Extended_Topology(void *arg)
{
	if(arg != NULL)
	{
		CORE *Core=(CORE *) arg;

		int	InputLevel=0, NoMoreLevels=0,
			SMT_Mask_Width=0, SMT_Select_Mask=0,
			CorePlus_Mask_Width=0, CoreOnly_Select_Mask=0;

		CPUID_TOPOLOGY_LEAF ExtTopology=
		{
			.AX.Register=0,
			.BX.Register=0,
			.CX.Register=0,
			.DX.Register=0
		};

		RDMSR(Core->T.Base, MSR_IA32_APICBASE);

		do
		{
			asm volatile
			(
				"cpuid"
				: "=a"	(ExtTopology.AX),
				  "=b"	(ExtTopology.BX),
				  "=c"	(ExtTopology.CX),
				  "=d"	(ExtTopology.DX)
				:  "a"	(0xb),
				   "c"	(InputLevel)
			);
			// Exit from the loop if the BX register equals 0 or
			// if the requested level exceeds the level of a Core.
			if(!ExtTopology.BX.Register
			|| (InputLevel > LEVEL_CORE))
				NoMoreLevels=1;
			else
			{
			    switch(ExtTopology.CX.Type)
			    {
			    case LEVEL_THREAD:
				{
				SMT_Mask_Width  = ExtTopology.AX.SHRbits;
				SMT_Select_Mask = ~((-1) << SMT_Mask_Width);
				Core->T.ThreadID= ExtTopology.DX.x2ApicID \
						& SMT_Select_Mask;
				}
			    break;
			    case LEVEL_CORE:
				{
				CorePlus_Mask_Width=ExtTopology.AX.SHRbits;
				CoreOnly_Select_Mask=			\
					(~((-1) << CorePlus_Mask_Width))\
					^ SMT_Select_Mask;
				Core->T.CoreID=				\
				    (ExtTopology.DX.x2ApicID		\
				    & CoreOnly_Select_Mask) >> SMT_Mask_Width;
				}
			    break;
			    }
			}
			InputLevel++;
		}
		while(!NoMoreLevels);

		Core->T.ApicID=ExtTopology.DX.x2ApicID;
		Cache_Topology(Core);

		complete_and_exit(&topology_job_complete, 0);
	}
	else
		complete_and_exit(&topology_job_complete, -1);
}

unsigned int Proc_Topology(void)
{
	unsigned int cpu=0, CountEnabledCPU=0;
	struct task_struct *tid;

	for(cpu=0; cpu < Proc->CPU.Count; cpu++)
	{
		KPublic->Core[cpu]->T.Base.value=0;
		KPublic->Core[cpu]->T.ApicID=-1;
		KPublic->Core[cpu]->T.CoreID=-1;
		KPublic->Core[cpu]->T.ThreadID=-1;
		if(!KPublic->Core[cpu]->OffLine)
		{
		  	tid=kthread_create(
				(Proc->Features.LargestStdFunc >= 0xb) ?
					Map_Extended_Topology : Map_Topology,
				KPublic->Core[cpu],
				"coreapic/%-3d",
				KPublic->Core[cpu]->Bind);
			if(!IS_ERR(tid))
			{
				kthread_bind(tid ,cpu);
				wake_up_process(tid);
				wait_for_completion(&topology_job_complete);

				if(KPublic->Core[cpu]->T.ApicID >= 0)
					CountEnabledCPU++;

				if(!Proc->Features.HTT_enabled
				&& (KPublic->Core[cpu]->T.ThreadID > 0))
					Proc->Features.HTT_enabled=1;

				reinit_completion(&topology_job_complete);
			}
		}
	}
	return(CountEnabledCPU);
}


void Counters_Set(CORE *Core)
{
	GLOBAL_PERF_COUNTER GlobalPerfCounter={.value=0};
	FIXED_PERF_COUNTER FixedPerfCounter={.value=0};
	GLOBAL_PERF_STATUS Overflow={.value=0};
	GLOBAL_PERF_OVF_CTRL OvfControl={.value=0};

	RDMSR(GlobalPerfCounter, MSR_CORE_PERF_GLOBAL_CTRL);

	Core->SaveArea.GlobalPerfCounter=GlobalPerfCounter;
	GlobalPerfCounter.EN_FIXED_CTR0=1;
	GlobalPerfCounter.EN_FIXED_CTR1=1;
	GlobalPerfCounter.EN_FIXED_CTR2=1;

	WRMSR(GlobalPerfCounter, MSR_CORE_PERF_GLOBAL_CTRL);

	RDMSR(FixedPerfCounter, MSR_CORE_PERF_FIXED_CTR_CTRL);

	Core->SaveArea.FixedPerfCounter=FixedPerfCounter;
	FixedPerfCounter.EN0_OS=1;
	FixedPerfCounter.EN1_OS=1;
	FixedPerfCounter.EN2_OS=1;
	FixedPerfCounter.EN0_Usr=1;
	FixedPerfCounter.EN1_Usr=1;
	FixedPerfCounter.EN2_Usr=1;
	if(Proc->PerCore)
	{
		FixedPerfCounter.AnyThread_EN0=1;
		FixedPerfCounter.AnyThread_EN1=1;
		FixedPerfCounter.AnyThread_EN2=1;
	}
	else	// Per Thread
	{
		FixedPerfCounter.AnyThread_EN0=0;
		FixedPerfCounter.AnyThread_EN1=0;
		FixedPerfCounter.AnyThread_EN2=0;
	}
	WRMSR(FixedPerfCounter, MSR_CORE_PERF_FIXED_CTR_CTRL);

	RDMSR(Overflow, MSR_CORE_PERF_GLOBAL_STATUS);
	if(Overflow.Overflow_CTR0)
		OvfControl.Clear_Ovf_CTR0=1;
	if(Overflow.Overflow_CTR1)
		OvfControl.Clear_Ovf_CTR1=1;
	if(Overflow.Overflow_CTR2)
		OvfControl.Clear_Ovf_CTR2=1;
	if(Overflow.Overflow_CTR0					\
	| Overflow.Overflow_CTR1					\
	| Overflow.Overflow_CTR2)
		WRMSR(OvfControl, MSR_CORE_PERF_GLOBAL_OVF_CTRL);
}

void Counters_Clear(CORE *Core)
{
	WRMSR(	Core->SaveArea.FixedPerfCounter, MSR_CORE_PERF_FIXED_CTR_CTRL);
	WRMSR(	Core->SaveArea.GlobalPerfCounter, MSR_CORE_PERF_GLOBAL_CTRL);
}

#define Counters_Genuine(Core, T)					\
({									\
	/* Actual & Maximum Performance Frequency Clock counters. */	\
	RDCOUNTER(Core->Counter[T].C0.UCC, MSR_IA32_APERF);		\
	RDCOUNTER(Core->Counter[T].C0.URC, MSR_IA32_MPERF);		\
									\
	/* TSC in relation to the Core.	*/				\
	RDCOUNTER(Core->Counter[T].TSC, MSR_IA32_TSC);			\
									\
	/* Derive C1 */							\
	Core->Counter[T].C1=						\
	  (Core->Counter[T].TSC > Core->Counter[T].C0.URC) ?		\
	    Core->Counter[T].TSC - Core->Counter[T].C0.URC		\
	    : 0;							\
})

#define Counters_Core2(Core, T)						\
({									\
	/* Instructions Retired. */					\
	RDCOUNTER(Core->Counter[T].INST, MSR_CORE_PERF_FIXED_CTR0);	\
									\
	/* Unhalted Core & Reference Cycles. */				\
	RDCOUNTER(Core->Counter[T].C0.UCC, MSR_CORE_PERF_FIXED_CTR1);	\
	RDCOUNTER(Core->Counter[T].C0.URC, MSR_CORE_PERF_FIXED_CTR2);	\
									\
	/* TSC in relation to the Logical Core. */			\
	RDCOUNTER(Core->Counter[T].TSC, MSR_IA32_TSC);			\
									\
	/* Derive C1 */							\
	Core->Counter[T].C1=						\
	  (Core->Counter[T].TSC > Core->Counter[T].C0.URC) ?		\
	    Core->Counter[T].TSC - Core->Counter[T].C0.URC		\
	    : 0;							\
})

#define Counters_Nehalem(Core, T)					\
({									\
	register unsigned long long Cx=0;				\
									\
	/* Instructions Retired. */					\
	RDCOUNTER(Core->Counter[T].INST, MSR_CORE_PERF_FIXED_CTR0);	\
									\
	/* Unhalted Core & Reference Cycles. */				\
	RDCOUNTER(Core->Counter[T].C0.UCC, MSR_CORE_PERF_FIXED_CTR1);	\
	RDCOUNTER(Core->Counter[T].C0.URC, MSR_CORE_PERF_FIXED_CTR2);	\
									\
	/* C-States. */							\
	RDCOUNTER(Core->Counter[T].C3, MSR_CORE_C3_RESIDENCY);		\
	RDCOUNTER(Core->Counter[T].C6, MSR_CORE_C6_RESIDENCY);		\
									\
	/* TSC in relation to the Logical Core. */			\
	RDCOUNTER(Core->Counter[T].TSC, MSR_IA32_TSC);			\
									\
	/* Derive C1 */							\
	Cx=	Core->Counter[T].C6					\
		+ Core->Counter[T].C3					\
		+ Core->Counter[T].C0.URC;				\
									\
	Core->Counter[T].C1=						\
		(Core->Counter[T].TSC > Cx) ?				\
			Core->Counter[T].TSC - Cx			\
			: 0;						\
})

#define Counters_SandyBridge(Core, T)					\
({									\
	register unsigned long long Cx=0;				\
									\
	/* Instructions Retired. */					\
	RDCOUNTER(Core->Counter[T].INST, MSR_CORE_PERF_FIXED_CTR0);	\
									\
	/* Unhalted Core & Reference Cycles. */				\
	RDCOUNTER(Core->Counter[T].C0.UCC, MSR_CORE_PERF_FIXED_CTR1);	\
	RDCOUNTER(Core->Counter[T].C0.URC, MSR_CORE_PERF_FIXED_CTR2);	\
									\
	/* TSC in relation to the Logical Core. */			\
	RDCOUNTER(Core->Counter[T].TSC, MSR_IA32_TSC);			\
									\
	/* C-States. */							\
	RDCOUNTER(Core->Counter[T].C3, MSR_CORE_C3_RESIDENCY);		\
	RDCOUNTER(Core->Counter[T].C6, MSR_CORE_C6_RESIDENCY);		\
	RDCOUNTER(Core->Counter[T].C7, MSR_CORE_C7_RESIDENCY);		\
									\
	/* Derive C1 */							\
	Cx=	Core->Counter[T].C7					\
		+ Core->Counter[T].C6					\
		+ Core->Counter[T].C3					\
		+ Core->Counter[T].C0.URC;				\
									\
	Core->Counter[T].C1=						\
		(Core->Counter[T].TSC > Cx) ?				\
			Core->Counter[T].TSC - Cx			\
			: 0;						\
})

void Core_Thermal(CORE *Core)
{
	RDMSR(Core->TjMax, MSR_IA32_TEMPERATURE_TARGET);
	if(!Core->TjMax.Target)
		Core->TjMax.Target=100;
}

#define Core_Temp(Core)							\
(									\
	RDMSR(Core->ThermStat, MSR_IA32_THERM_STATUS)			\
)

int Cycle_Genuine(void *arg)
{
	if(arg != NULL)
	{
		CORE *Core=(CORE *) arg;

		struct completion *elapsed=&KPrivate->Join[Core->Bind]->Elapsed;
		unsigned int timeout=msecs_to_jiffies(Proc->msleep + 9);

		Counters_Genuine(Core, 0);
		Core_Thermal(Core);

		while(!kthread_should_stop())
		{
		    if(wait_for_completion_timeout(elapsed, timeout))
		    {
			reinit_completion(elapsed);

			Counters_Genuine(Core, 1);
			Core_Temp(Core);

			// Absolute Delta of Unhalted (Core & Ref) C0 Counter.
			Core->Delta.C0.UCC=				\
				(Core->Counter[0].C0.UCC >		\
				Core->Counter[1].C0.UCC) ?		\
					Core->Counter[0].C0.UCC		\
					- Core->Counter[1].C0.UCC	\
					: Core->Counter[1].C0.UCC	\
					- Core->Counter[0].C0.UCC;

			Core->Delta.C0.URC= Core->Counter[1].C0.URC	\
					  - Core->Counter[0].C0.URC;

			Core->Delta.TSC = Core->Counter[1].TSC		\
					- Core->Counter[0].TSC;

			Core->Delta.C1=					\
				(Core->Counter[0].C1 >			\
				 Core->Counter[1].C1) ?			\
					Core->Counter[0].C1		\
					- Core->Counter[1].C1		\
					: Core->Counter[1].C1		\
					- Core->Counter[0].C1;

			// Save TSC.
			Core->Counter[0].TSC=Core->Counter[1].TSC;

			// Save the Unhalted Core & Reference Counter
			// for next iteration.
			Core->Counter[0].C0.UCC=Core->Counter[1].C0.UCC;
			Core->Counter[0].C0.URC=Core->Counter[1].C0.URC;

			Core->Counter[0].C1=Core->Counter[1].C1;

			BITSET(Core->Sync.V, 0);
		    }
		}
	}
	do_exit(0);
}

void Arch_Genuine(unsigned int stage)
{
	unsigned int cpu=0;

	switch(stage)
	{
		case END:
		{
		}
		break;
		case INIT:
		{
			PLATFORM_INFO Platform={.value=0};

			RDMSR(Platform, MSR_PLATFORM_INFO);

			if(Platform.value != 0)
			{
			  Proc->Boost[0]=MINCOUNTER(Platform.MinimumRatio,\
						Platform.MaxNonTurboRatio);
			  Proc->Boost[1]=MAXCOUNTER(Platform.MinimumRatio,\
						Platform.MaxNonTurboRatio);
			}
			Proc->Boost[9]=Proc->Boost[1];

			if(AutoClock)
				Proc->Clock=Base_Clock(Proc->Boost[1]);
			else
			    switch(Proc->ArchID)
			    {
			    case Core_Yonah:
				Proc->Clock=Clock_Core(Proc->Boost[1]);
			    break;
			    default:
				Proc->Clock=Clock_GenuineIntel(Proc->Boost[1]);
			    break;
			    }
		}
		break;
		case STOP:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine
			    && !IS_ERR(KPublic->Core[cpu]->TID))
			    {
				complete(&KPrivate->Join[cpu]->Elapsed);
				kthread_stop(KPublic->Core[cpu]->TID);
			    }
		}
		break;
		case START:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine)
			    {
				KPublic->Core[cpu]->TID= \
					kthread_create(	Cycle_Genuine,
							KPublic->Core[cpu],
							"corefreqk/%-3d",
							KPublic->Core[cpu]->Bind);
				if(!IS_ERR(KPublic->Core[cpu]->TID))
					kthread_bind(KPublic->Core[cpu]->TID, cpu);
			   }
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
				if(!KPublic->Core[cpu]->OffLine
				&& !IS_ERR(KPublic->Core[cpu]->TID))
					wake_up_process(KPublic->Core[cpu]->TID);
		}
		break;
	}
}

int Cycle_Core2(void *arg)
{
	if(arg != NULL)
	{
		CORE *Core=(CORE *) arg;

		struct completion *elapsed=&KPrivate->Join[Core->Bind]->Elapsed;
		unsigned int timeout=msecs_to_jiffies(Proc->msleep + 9);

		Counters_Set(Core);
		Counters_Core2(Core, 0);
		Core_Thermal(Core);

		while(!kthread_should_stop())
		{
		    if(wait_for_completion_timeout(elapsed, timeout))
		    {
			reinit_completion(elapsed);

			Counters_Core2(Core, 1);
			Core_Temp(Core);

			// Delta of Instructions Retired
			Core->Delta.INST= Core->Counter[1].INST		\
					- Core->Counter[0].INST;

			// Absolute Delta of Unhalted (Core & Ref) C0 Counter.
			Core->Delta.C0.UCC=				\
				(Core->Counter[0].C0.UCC >		\
				Core->Counter[1].C0.UCC) ?		\
					Core->Counter[0].C0.UCC		\
					- Core->Counter[1].C0.UCC	\
					: Core->Counter[1].C0.UCC	\
					- Core->Counter[0].C0.UCC;

			Core->Delta.C0.URC= Core->Counter[1].C0.URC	\
					  - Core->Counter[0].C0.URC;

			Core->Delta.TSC = Core->Counter[1].TSC		\
					- Core->Counter[0].TSC;

			Core->Delta.C1=					\
				(Core->Counter[0].C1 >			\
				 Core->Counter[1].C1) ?			\
					Core->Counter[0].C1		\
					- Core->Counter[1].C1		\
					: Core->Counter[1].C1		\
					- Core->Counter[0].C1;

			// Save the Instructions counter.
			Core->Counter[0].INST=Core->Counter[1].INST;

			// Save TSC.
			Core->Counter[0].TSC=Core->Counter[1].TSC;

			// Save the Unhalted Core & Reference Counter
			// for next iteration.
			Core->Counter[0].C0.UCC=Core->Counter[1].C0.UCC;
			Core->Counter[0].C0.URC=Core->Counter[1].C0.URC;

			Core->Counter[0].C1=Core->Counter[1].C1;

			BITSET(Core->Sync.V, 0);
		    }
		}
		Counters_Clear(Core);
	}
	do_exit(0);
}

void Arch_Core2(unsigned int stage)
{
	unsigned int cpu=0;

	switch(stage)
	{
		case END:
		{
		}
		break;
		case INIT:
		{
			PLATFORM_INFO Platform={.value=0};

			RDMSR(Platform, MSR_PLATFORM_INFO);

			if(Platform.value != 0)
			{
			  Proc->Boost[0]=MINCOUNTER(Platform.MinimumRatio,\
						Platform.MaxNonTurboRatio);
			  Proc->Boost[1]=MAXCOUNTER(Platform.MinimumRatio,\
						Platform.MaxNonTurboRatio);
			}
			Proc->Boost[9]=Proc->Boost[1];

			if(AutoClock)
				Proc->Clock=Base_Clock(Proc->Boost[1]);
			else
			    switch(Proc->ArchID)
			    {
			    case Core_Conroe:
			    case Core_Kentsfield:
			    case Core_Yorkfield:
			    case Core_Dunnington:
				Proc->Clock=Clock_Core2(Proc->Boost[1]);
			    break;
			    case Atom_Bonnell:
			    case Atom_Silvermont:
			    case Atom_Lincroft:
			    case Atom_Clovertrail:
			    case Atom_Saltwell:
			    case Atom_Airmont:
			    case Atom_Goldmont:
			    case Atom_Sofia:
			    case Atom_Merrifield:
			    case Atom_Moorefield:
				Proc->Clock=Clock_Atom(Proc->Boost[1]);
			    default:
				Proc->Clock=Clock_GenuineIntel(Proc->Boost[1]);
			    break;
			}
		}
		break;
		case STOP:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine
			    && !IS_ERR(KPublic->Core[cpu]->TID))
			    {
				complete(&KPrivate->Join[cpu]->Elapsed);
				kthread_stop(KPublic->Core[cpu]->TID);
			    }
		}
		break;
		case START:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine)
			    {
				KPublic->Core[cpu]->TID= \
					kthread_create(	Cycle_Core2,
							KPublic->Core[cpu],
							"corefreqk/%-3d",
							KPublic->Core[cpu]->Bind);
				if(!IS_ERR(KPublic->Core[cpu]->TID))
					kthread_bind(KPublic->Core[cpu]->TID, cpu);
			    }
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
				if(!KPublic->Core[cpu]->OffLine
				&& !IS_ERR(KPublic->Core[cpu]->TID))
					wake_up_process(KPublic->Core[cpu]->TID);
		}
		break;
	}
}

int Cycle_Nehalem(void *arg)
{
	if(arg != NULL)
	{
		CORE *Core=(CORE *) arg;

		struct completion *elapsed=&KPrivate->Join[Core->Bind]->Elapsed;
		unsigned int timeout=msecs_to_jiffies(Proc->msleep + 9);

		Counters_Set(Core);
		Counters_Nehalem(Core, 0);
		Core_Thermal(Core);

		while(!kthread_should_stop())
		{
		    if(wait_for_completion_timeout(elapsed, timeout))
		    {
			reinit_completion(elapsed);

			Counters_Nehalem(Core, 1);
			Core_Temp(Core);

			// Delta of Instructions Retired
			Core->Delta.INST= Core->Counter[1].INST		\
					- Core->Counter[0].INST;

			// Absolute Delta of Unhalted (Core & Ref) C0 Counter.
			Core->Delta.C0.UCC=				\
				(Core->Counter[0].C0.UCC >		\
				Core->Counter[1].C0.UCC) ?		\
					Core->Counter[0].C0.UCC		\
					- Core->Counter[1].C0.UCC	\
					: Core->Counter[1].C0.UCC	\
					- Core->Counter[0].C0.UCC;

			Core->Delta.C0.URC= Core->Counter[1].C0.URC	\
					  - Core->Counter[0].C0.URC;

			Core->Delta.C3  = Core->Counter[1].C3		\
					- Core->Counter[0].C3;
			Core->Delta.C6  = Core->Counter[1].C6		\
					- Core->Counter[0].C6;

			Core->Delta.TSC = Core->Counter[1].TSC		\
					- Core->Counter[0].TSC;

			Core->Delta.C1=					\
				(Core->Counter[0].C1 >			\
				 Core->Counter[1].C1) ?			\
					Core->Counter[0].C1		\
					- Core->Counter[1].C1		\
					: Core->Counter[1].C1		\
					- Core->Counter[0].C1;

			// Save the Instructions counter.
			Core->Counter[0].INST=Core->Counter[1].INST;

			// Save TSC.
			Core->Counter[0].TSC=Core->Counter[1].TSC;

			// Save the Unhalted Core & Reference Counter
			// for next iteration.
			Core->Counter[0].C0.UCC=Core->Counter[1].C0.UCC;
			Core->Counter[0].C0.URC=Core->Counter[1].C0.URC;

			// Save also the C-State Reference Counter.
			Core->Counter[0].C3=Core->Counter[1].C3;
			Core->Counter[0].C6=Core->Counter[1].C6;
			Core->Counter[0].C1=Core->Counter[1].C1;

			BITSET(Core->Sync.V, 0);
		    }
		}
		Counters_Clear(Core);
	}
	do_exit(0);
}

void Arch_Nehalem(unsigned int stage)
{
	unsigned int cpu=0;

	switch(stage)
	{
		case END:
		{
		}
		break;
		case INIT:
		{
			PLATFORM_INFO Platform={.value=0};
			TURBO_RATIO Turbo={.value=0};

			RDMSR(Platform, MSR_PLATFORM_INFO);
			RDMSR(Turbo, MSR_NHM_TURBO_RATIO_LIMIT);

			Proc->Boost[0]=Platform.MinimumRatio;
			Proc->Boost[1]=Platform.MaxNonTurboRatio;
			Proc->Boost[2]=Turbo.MaxRatio_8C;
			Proc->Boost[3]=Turbo.MaxRatio_7C;
			Proc->Boost[4]=Turbo.MaxRatio_6C;
			Proc->Boost[5]=Turbo.MaxRatio_5C;
			Proc->Boost[6]=Turbo.MaxRatio_4C;
			Proc->Boost[7]=Turbo.MaxRatio_3C;
			Proc->Boost[8]=Turbo.MaxRatio_2C;
			Proc->Boost[9]=Turbo.MaxRatio_1C;

			if(AutoClock)
				Proc->Clock=Base_Clock(Proc->Boost[1]);
			else
			    switch(Proc->ArchID)
			    {
			    case Silvermont_637:
			    case Silvermont_64D:
				Proc->Clock=Clock_Silvermont(Proc->Boost[1]);
			    break;
			    case Nehalem_Bloomfield:
			    case Nehalem_Lynnfield:
			    case Nehalem_MB:
			    case Nehalem_EX:
				Proc->Clock=Clock_Nehalem(Proc->Boost[1]);
			    break;
			    case Westmere:
			    case Westmere_EP:
			    case Westmere_EX:
				Proc->Clock=Clock_Westmere(Proc->Boost[1]);
			    break;
			    default:
				Proc->Clock=Clock_GenuineIntel(Proc->Boost[1]);
			    break;
			}
		}
		break;
		case STOP:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine
			    && !IS_ERR(KPublic->Core[cpu]->TID))
			    {
				complete(&KPrivate->Join[cpu]->Elapsed);
				kthread_stop(KPublic->Core[cpu]->TID);
			    }
		}
		break;
		case START:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine)
			    {
				KPublic->Core[cpu]->TID= \
					kthread_create(	Cycle_Nehalem,
							KPublic->Core[cpu],
							"corefreqk/%-3d",
							KPublic->Core[cpu]->Bind);
				if(!IS_ERR(KPublic->Core[cpu]->TID))
					kthread_bind(KPublic->Core[cpu]->TID, cpu);
			    }
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
				if(!KPublic->Core[cpu]->OffLine
				&& !IS_ERR(KPublic->Core[cpu]->TID))
					wake_up_process(KPublic->Core[cpu]->TID);
		}
		break;
	}
}

int Cycle_SandyBridge(void *arg)
{
	if(arg != NULL)
	{
		CORE *Core=(CORE *) arg;

		struct completion *elapsed=&KPrivate->Join[Core->Bind]->Elapsed;
		unsigned int timeout=msecs_to_jiffies(Proc->msleep + 9);

		Counters_Set(Core);
		Counters_SandyBridge(Core, 0);
		Core_Thermal(Core);

		while(!kthread_should_stop())
		{
		    if(wait_for_completion_timeout(elapsed, timeout))
		    {
			reinit_completion(elapsed);

			Counters_SandyBridge(Core, 1);
			Core_Temp(Core);

			// Delta of Instructions Retired
			Core->Delta.INST= Core->Counter[1].INST		\
					- Core->Counter[0].INST;

			// Absolute Delta of Unhalted (Core & Ref) C0 Counter.
			Core->Delta.C0.UCC=				\
				(Core->Counter[0].C0.UCC >		\
				Core->Counter[1].C0.UCC) ?		\
					Core->Counter[0].C0.UCC		\
					- Core->Counter[1].C0.UCC	\
					: Core->Counter[1].C0.UCC	\
					- Core->Counter[0].C0.UCC;

			Core->Delta.C0.URC= Core->Counter[1].C0.URC	\
					  - Core->Counter[0].C0.URC;

			Core->Delta.C3  = Core->Counter[1].C3		\
					- Core->Counter[0].C3;
			Core->Delta.C6  = Core->Counter[1].C6		\
					- Core->Counter[0].C6;
			Core->Delta.C7  = Core->Counter[1].C7		\
					- Core->Counter[0].C7;

			Core->Delta.TSC = Core->Counter[1].TSC		\
					- Core->Counter[0].TSC;

			Core->Delta.C1=					\
				(Core->Counter[0].C1 >			\
				 Core->Counter[1].C1) ?			\
					Core->Counter[0].C1		\
					- Core->Counter[1].C1		\
					: Core->Counter[1].C1		\
					- Core->Counter[0].C1;

			// Save the Instructions counter.
			Core->Counter[0].INST=Core->Counter[1].INST;

			// Save TSC.
			Core->Counter[0].TSC=Core->Counter[1].TSC;

			// Save the Unhalted Core & Reference Counter
			// for next iteration.
			Core->Counter[0].C0.UCC=Core->Counter[1].C0.UCC;
			Core->Counter[0].C0.URC=Core->Counter[1].C0.URC;

			// Save also the C-State Reference Counter.
			Core->Counter[0].C3=Core->Counter[1].C3;
			Core->Counter[0].C6=Core->Counter[1].C6;
			Core->Counter[0].C7=Core->Counter[1].C7;
			Core->Counter[0].C1=Core->Counter[1].C1;

			BITSET(Core->Sync.V, 0);
		    }
		}
		Counters_Clear(Core);
	}
	do_exit(0);
}

void Arch_SandyBridge(unsigned int stage)
{

	unsigned int cpu=0;

	switch(stage)
	{
		case END:
		{
		}
		break;
		case INIT:
		{
			PLATFORM_INFO Platform={.value=0};
			TURBO_RATIO Turbo={.value=0};

			RDMSR(Platform, MSR_PLATFORM_INFO);
			RDMSR(Turbo, MSR_NHM_TURBO_RATIO_LIMIT);

			Proc->Boost[0]=Platform.MinimumRatio;
			Proc->Boost[1]=Platform.MaxNonTurboRatio;
			Proc->Boost[2]=Turbo.MaxRatio_8C;
			Proc->Boost[3]=Turbo.MaxRatio_7C;
			Proc->Boost[4]=Turbo.MaxRatio_6C;
			Proc->Boost[5]=Turbo.MaxRatio_5C;
			Proc->Boost[6]=Turbo.MaxRatio_4C;
			Proc->Boost[7]=Turbo.MaxRatio_3C;
			Proc->Boost[8]=Turbo.MaxRatio_2C;
			Proc->Boost[9]=Turbo.MaxRatio_1C;

			if(AutoClock)
				Proc->Clock=Base_Clock(Proc->Boost[1]);
			else
			    switch(Proc->ArchID)
			    {
			    case SandyBridge:
			    case SandyBridge_EP:
				Proc->Clock=Clock_SandyBridge(Proc->Boost[1]);
			    break;
			    case IvyBridge:
			    case IvyBridge_EP:
				Proc->Clock=Clock_IvyBridge(Proc->Boost[1]);
			    break;
			    case Haswell_DT:
			    case Haswell_MB:
			    case Haswell_ULT:
			    case Haswell_ULX:
			    case Broadwell:
			    case Broadwell_EP:
			    case Broadwell_H:
			    case Broadwell_EX:
			    case Skylake_UY:
			    case Skylake_S:
			    case Skylake_E:
				Proc->Clock=Clock_Haswell(Proc->Boost[1]);
			    break;
			    default:
				Proc->Clock=Clock_GenuineIntel(Proc->Boost[1]);
			    break;
			}
		}
		break;
		case STOP:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine
			    && !IS_ERR(KPublic->Core[cpu]->TID))
			    {
				complete(&KPrivate->Join[cpu]->Elapsed);
				kthread_stop(KPublic->Core[cpu]->TID);
			    }
		}
		break;
		case START:
		{
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
			    if(!KPublic->Core[cpu]->OffLine)
			    {
				KPublic->Core[cpu]->TID= \
					kthread_create(	Cycle_SandyBridge,
							KPublic->Core[cpu],
							"corefreqk/%-3d",
							KPublic->Core[cpu]->Bind);
				if(!IS_ERR(KPublic->Core[cpu]->TID))
					kthread_bind(KPublic->Core[cpu]->TID, cpu);
			    }
			for(cpu=0; cpu < Proc->CPU.Count; cpu++)
				if(!KPublic->Core[cpu]->OffLine
				&& !IS_ERR(KPublic->Core[cpu]->TID))
					wake_up_process(KPublic->Core[cpu]->TID);
		}
		break;
	}
}

static struct hrtimer Timer;
static ktime_t RearmTheTimer;

static enum hrtimer_restart Cycle_Timer(struct hrtimer *pTimer)
{
	unsigned int cpu=0;
	for(cpu=0; cpu < Proc->CPU.Count; cpu++)
		complete(&KPrivate->Join[cpu]->Elapsed);

	hrtimer_forward(pTimer, hrtimer_cb_get_time(pTimer), RearmTheTimer);

	return(HRTIMER_RESTART);
}

void InitTimer(void)
{
	unsigned int cpu=0;
	for(cpu=0; cpu < Proc->CPU.Count; cpu++)
		init_completion(&KPrivate->Join[cpu]->Elapsed);

	RearmTheTimer=ktime_set(0, Proc->msleep * 1000000L);
	hrtimer_init(&Timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	Timer.function=&Cycle_Timer;
}

void StartTimer(void)
{
	hrtimer_start(&Timer, RearmTheTimer, HRTIMER_MODE_REL);
}

void StopTimer(void)
{
	hrtimer_cancel(&Timer);
}

static int CoreFreqK_mmap(struct file *pfile, struct vm_area_struct *vma)
{
	if(vma->vm_pgoff == 0)
	{
	    if((Proc != NULL)
	    && remap_pfn_range(	vma,
				vma->vm_start,
				virt_to_phys((void *) Proc) >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot) < 0)
		return(-EIO);
	}
	else
	{
	    unsigned int cpu=vma->vm_pgoff - 1;
	    if((cpu < Proc->CPU.Count) && (cpu >= 0)
	    && (KPublic->Core[cpu] != NULL)
	    && remap_pfn_range(vma,
			vma->vm_start,
			virt_to_phys((void *) KPublic->Core[cpu]) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start,
			vma->vm_page_prot) < 0)
		return(-EIO);
	}
	return(0);
}

static DEFINE_MUTEX(CoreFreqK_mutex);

static int CoreFreqK_open(struct inode *inode, struct file *pfile)
{
	if(!mutex_trylock(&CoreFreqK_mutex))
		return(-EBUSY);
	else
		return(0);
}

static int CoreFreqK_release(struct inode *inode, struct file *pfile)
{
	mutex_unlock(&CoreFreqK_mutex);
	return(0);
}

static struct file_operations CoreFreqK_fops=
{
	.open	= CoreFreqK_open,
	.release= CoreFreqK_release,
	.mmap	= CoreFreqK_mmap,
	.owner  = THIS_MODULE,
};

#ifdef CONFIG_PM_SLEEP
static int CoreFreqK_suspend(struct device *dev)
{
        Arch[Proc->ArchID].Arch_Controller(STOP);

        StopTimer();

        printk(KERN_INFO "CoreFreq: Suspend\n");
        return(0);
}

static int CoreFreqK_resume(struct device *dev)
{
        Arch[Proc->ArchID].Arch_Controller(START);

        StartTimer();

        printk(KERN_INFO "CoreFreq: Resume\n");
        return(0);
}

static SIMPLE_DEV_PM_OPS(CoreFreqK_pm_ops, CoreFreqK_suspend, CoreFreqK_resume);
#define COREFREQ_PM_OPS (&CoreFreqK_pm_ops)
#else
#define COREFREQ_PM_OPS NULL
#endif


static int __init CoreFreqK_init(void)
{
	int rc=0;
	CoreFreqK.kcdev=cdev_alloc();
	CoreFreqK.kcdev->ops=&CoreFreqK_fops;
	CoreFreqK.kcdev->owner=THIS_MODULE;

        if(alloc_chrdev_region(&CoreFreqK.nmdev, 0, 1, DRV_FILENAME) >= 0)
	{
	    CoreFreqK.Major=MAJOR(CoreFreqK.nmdev);
	    CoreFreqK.mkdev=MKDEV(CoreFreqK.Major, 0);

	    if(cdev_add(CoreFreqK.kcdev, CoreFreqK.mkdev, 1) >= 0)
	    {
		struct device *tmpDev;

		CoreFreqK.clsdev=class_create(THIS_MODULE, DRV_DEVNAME);
		CoreFreqK.clsdev->pm=COREFREQ_PM_OPS;

		if((tmpDev=device_create(CoreFreqK.clsdev, NULL,
					 CoreFreqK.mkdev, NULL,
					 DRV_DEVNAME)) != NULL)
		{
		    unsigned int cpu=0, count=Core_Count();
		    unsigned long publicSize=0, privateSize=0, packageSize=0;

		    publicSize=sizeof(KPUBLIC) + sizeof(CORE *) * count;

		    privateSize=sizeof(KPRIVATE)
				+ sizeof(struct completion) * count;

		    if(((KPublic=kmalloc(publicSize, GFP_KERNEL)) != NULL)
		    && ((KPrivate=kmalloc(privateSize, GFP_KERNEL)) != NULL))
		    {
			memset(KPublic, 0, publicSize);
			memset(KPrivate, 0, privateSize);

			packageSize=ROUND_TO_PAGES(sizeof(PROC));
			if((Proc=kmalloc(packageSize, GFP_KERNEL)) != NULL)
			{
			    memset(Proc, 0, packageSize);
			    Proc->CPU.Count=count;
			    Proc->msleep=LOOP_DEF_MS;
			    Proc->PerCore=0;
			    Proc_Features(&Proc->Features);
			    Proc->Features.FactoryFreq=			\
					Proc_Brand(Proc->Features.Brand);

			    Arch[0].Architecture=Proc->Features.VendorID;

			    publicSize=ROUND_TO_PAGES(sizeof(CORE));
			    privateSize=ROUND_TO_PAGES(sizeof(JOIN));
			    if(((KPublic->Cache=kmem_cache_create(
					"corefreqk-pub",
					publicSize, 0,
					SLAB_HWCACHE_ALIGN, NULL)) != NULL)
			    && ((KPrivate->Cache=kmem_cache_create(
					"corefreqk-priv",
					privateSize, 0,
					SLAB_HWCACHE_ALIGN, NULL)) != NULL))
			    {
				for(cpu=0; cpu < Proc->CPU.Count; cpu++)
				{
				    void *kcache=kmem_cache_alloc(
						KPublic->Cache, GFP_KERNEL);
				    memset(kcache, 0, publicSize);
				    KPublic->Core[cpu]=kcache;

				    kcache=kmem_cache_alloc(
						KPrivate->Cache, GFP_KERNEL);
				    KPrivate->Join[cpu]=kcache;

				    BITCLR(KPublic->Core[cpu]->Sync.V, 0);

				    KPublic->Core[cpu]->Bind=cpu;
				    if(!cpu_online(cpu) || !cpu_active(cpu))
					KPublic->Core[cpu]->OffLine=1;
				    else
					KPublic->Core[cpu]->OffLine=0;
				}
				if((ArchID != -1)
				&& (ArchID >= 0)
				&& (ArchID < ARCHITECTURES))
					Proc->ArchID=ArchID;
				else
				{
				  for(	Proc->ArchID=ARCHITECTURES - 1;
					Proc->ArchID > 0;
					Proc->ArchID--)
				  // Search for an architecture signature.
				    if(!(Arch[Proc->ArchID].Signature.ExtFamily
				    ^ Proc->Features.Std.AX.ExtFamily)
				    && !(Arch[Proc->ArchID].Signature.Family
				    ^ Proc->Features.Std.AX.Family)
				    && !(Arch[Proc->ArchID].Signature.ExtModel
				    ^ Proc->Features.Std.AX.ExtModel)
				    && !(Arch[Proc->ArchID].Signature.Model
				    ^ Proc->Features.Std.AX.Model))
					break;
				}
				strncpy(Proc->Architecture,
					Arch[Proc->ArchID].Architecture, 32);

				Arch[Proc->ArchID].Arch_Controller(INIT);

				InitTimer();

				Proc->CPU.OnLine=Proc_Topology();
				Proc->PerCore=(Proc->Features.HTT_enabled)?0:1;

				printk(KERN_INFO "CoreFreq:"			\
				      " Processor [%1X%1X_%1X%1X]"		\
				      " Architecture [%s]\n",
					Arch[Proc->ArchID].Signature.ExtFamily,
					Arch[Proc->ArchID].Signature.Family,
					Arch[Proc->ArchID].Signature.ExtModel,
					Arch[Proc->ArchID].Signature.Model,
					Arch[Proc->ArchID].Architecture);
				printk(KERN_INFO "CoreFreq:"			\
					" %u/%u CPU Online."			\
					" Base Clock @ %llu Hz\n",
					Proc->CPU.OnLine,
					Proc->CPU.Count,
					Proc->Clock.Hz);

				Arch[Proc->ArchID].Arch_Controller(START);

				StartTimer();
			    }
			    else
			    {
				if(KPublic->Cache != NULL)
					kmem_cache_destroy(KPublic->Cache);
				if(KPrivate->Cache != NULL)
					kmem_cache_destroy(KPrivate->Cache);

				kfree(Proc);
				kfree(KPublic);
				kfree(KPrivate);

				device_destroy(CoreFreqK.clsdev,
					       CoreFreqK.mkdev);
				class_destroy(CoreFreqK.clsdev);
				cdev_del(CoreFreqK.kcdev);
				unregister_chrdev_region(CoreFreqK.mkdev, 1);

				rc=-ENOMEM;
			    }
			}
			else
			{
			    kfree(KPublic);
			    kfree(KPrivate);

			    device_destroy(CoreFreqK.clsdev,
					   CoreFreqK.mkdev);
			    class_destroy(CoreFreqK.clsdev);
			    cdev_del(CoreFreqK.kcdev);
			    unregister_chrdev_region(CoreFreqK.mkdev, 1);

			    rc=-ENOMEM;
			}
		    }
		    else
		    {
			if(KPublic != NULL)
				kfree(KPublic);
			if(KPrivate != NULL)
				kfree(KPrivate);

			device_destroy(CoreFreqK.clsdev, CoreFreqK.mkdev);
			class_destroy(CoreFreqK.clsdev);
			cdev_del(CoreFreqK.kcdev);
			unregister_chrdev_region(CoreFreqK.mkdev, 1);

			rc=-ENOMEM;
		    }
		}
		else
		{
		    class_destroy(CoreFreqK.clsdev);
		    cdev_del(CoreFreqK.kcdev);
		    unregister_chrdev_region(CoreFreqK.mkdev, 1);

		    rc=-EBUSY;
		}
	    }
	    else
	    {
		cdev_del(CoreFreqK.kcdev);
		unregister_chrdev_region(CoreFreqK.mkdev, 1);

		rc=-EBUSY;
	    }
	}
	else
	{
	    cdev_del(CoreFreqK.kcdev);

	    rc=-EBUSY;
	}
	return(rc);
}

static void __exit CoreFreqK_cleanup(void)
{
	unsigned int cpu=0;

	Arch[Proc->ArchID].Arch_Controller(STOP);
	Arch[Proc->ArchID].Arch_Controller(END);

	StopTimer();

	for(cpu=0; (KPublic->Cache != NULL) && (cpu < Proc->CPU.Count); cpu++)
	{
		if(KPublic->Core[cpu] != NULL)
			kmem_cache_free(KPublic->Cache,	KPublic->Core[cpu]);
		if(KPrivate->Join[cpu] != NULL)
			kmem_cache_free(KPrivate->Cache, KPrivate->Join[cpu]);
	}
	if(KPublic->Cache != NULL)
		kmem_cache_destroy(KPublic->Cache);
	if(KPrivate->Cache != NULL)
		kmem_cache_destroy(KPrivate->Cache);
	if(Proc != NULL)
		kfree(Proc);
	if(KPublic != NULL)
		kfree(KPublic);
	if(KPrivate != NULL)
		kfree(KPrivate);

	device_destroy(CoreFreqK.clsdev, CoreFreqK.mkdev);
	class_destroy(CoreFreqK.clsdev);
	cdev_del(CoreFreqK.kcdev);
	unregister_chrdev_region(CoreFreqK.mkdev, 1);

	printk(KERN_INFO "CoreFreq: Exit\n");
}

module_init(CoreFreqK_init);
module_exit(CoreFreqK_cleanup);