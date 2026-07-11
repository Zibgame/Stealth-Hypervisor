#ifndef HV_TRANSPARENT_SVM_LOADER_H
#define HV_TRANSPARENT_SVM_LOADER_H

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdint.h>

#define HV_BUILD_TAG "transparent-svm-v58-multicore-clean"
#define HV_VIRTUALIZE_ALL_CPUS 1
#define HV_MAX_CPUS 256
#define HV_HOST_STACK_PAGES 16
#define HV_LOG_ENTRIES 1024
#define HV_END_OF_CPU_LIST ((UINTN)0xffffffffU)

#define SVM_MSR_VM_CR       0xC0010114U
#define SVM_MSR_HSAVE_PA    0xC0010117U
#define MSR_EFER            0xC0000080U
#define MSR_PAT             0x00000277U
#define MSR_APIC_BASE       0x0000001BU
#define MSR_X2APIC_ICR      0x00000830U
#define EFER_SVME           (1ULL << 12)
/* VMCB guest EFER must keep SVME=1; guest-visible RDMSR shadows it to 0. */
#define VM_CR_R_INIT        (1ULL << 1)

#define SVM_EXIT_CPUID      0x72ULL
#define SVM_EXIT_MSR        0x7CULL
#define SVM_EXIT_SHUTDOWN   0x7FULL
#define SVM_EXIT_VMRUN      0x80ULL
#define SVM_EXIT_VMMCALL    0x81ULL
#define SVM_EXIT_SX         0x5EULL
#define SVM_EXIT_NPF        0x400ULL

#define SVM_INTERCEPT_CPUID (1U << 18)
#define SVM_INTERCEPT_MSR   (1U << 28)
#define SVM_INTERCEPT_SHUTDOWN (1U << 31)
#define SVM_INTERCEPT_VMRUN (1U << 0)
#define SVM_INTERCEPT_VMMCALL (1U << 1)
#define SVM_V_INTR_MASKING  (1ULL << 24)

typedef struct {
    uint16_t selector;
    uint16_t attrib;
    uint32_t limit;
    uint64_t base;
} __attribute__((packed)) HV_SEGMENT;

typedef struct {
    uint16_t intercept_cr_read;
    uint16_t intercept_cr_write;
    uint16_t intercept_dr_read;
    uint16_t intercept_dr_write;
    uint32_t intercept_exception;
    uint32_t intercept_misc1;
    uint32_t intercept_misc2;
    uint8_t reserved_014[0x3c - 0x14];
    uint16_t pause_filter_threshold;
    uint16_t pause_filter_count;
    uint64_t iopm_base_pa;
    uint64_t msrpm_base_pa;
    uint64_t tsc_offset;
    uint32_t guest_asid;
    uint32_t tlb_control;
    uint64_t v_intr;
    uint64_t interrupt_shadow;
    uint64_t exit_code;
    uint64_t exit_info1;
    uint64_t exit_info2;
    uint64_t exit_int_info;
    uint64_t np_enable;
    uint64_t avic_apic_bar;
    uint64_t ghcb;
    uint64_t event_inj;
    uint64_t ncr3;
    uint64_t lbr_virtualization;
    uint32_t vmcb_clean;
    uint32_t reserved_0c4;
    uint64_t nrip;
    uint8_t bytes_fetched;
    uint8_t instruction_bytes[15];
    uint8_t reserved_0e0[0x400 - 0x0e0];
} __attribute__((packed)) HV_VMCB_CONTROL;

typedef struct {
    HV_SEGMENT es, cs, ss, ds, fs, gs, gdtr, ldtr, idtr, tr;
    uint8_t reserved_0a0[0xcb - 0xa0];
    uint8_t cpl;
    uint32_t reserved_0cc;
    uint64_t efer;
    uint8_t reserved_0d8[0x148 - 0xd8];
    uint64_t cr4, cr3, cr0, dr7, dr6, rflags, rip;
    uint8_t reserved_180[0x1d8 - 0x180];
    uint64_t rsp;
    uint8_t reserved_1e0[0x1f8 - 0x1e0];
    uint64_t rax, star, lstar, cstar, sfmask, kernel_gs_base;
    uint64_t sysenter_cs, sysenter_esp, sysenter_eip, cr2;
    uint8_t reserved_248[0x268 - 0x248];
    uint64_t gpat, dbgctl, br_from, br_to, last_excp_from, last_excp_to;
} __attribute__((packed)) HV_VMCB_STATE;

typedef struct {
    HV_VMCB_CONTROL control;
    HV_VMCB_STATE state;
    uint8_t reserved[0x1000 - 0x400 - sizeof(HV_VMCB_STATE)];
} __attribute__((packed, aligned(4096))) HV_VMCB;

/* Must match svm_context_world.S. RAX lives in the VMCB; this holds the live GPRs. */
typedef struct {
    uint64_t rbx, rcx, rdx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
} HV_GPRS;

typedef struct {
    uint64_t sequence;
    uint32_t cpu;
    uint32_t event;
    uint64_t a, b, c, d;
} HV_LOG_ENTRY;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t cpu_count;
    volatile uint64_t write_index;
    HV_LOG_ENTRY entries[HV_LOG_ENTRIES];
} HV_LOG_RING;

typedef struct { uint16_t limit; uint64_t base; } __attribute__((packed)) DTR;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) HV_IDT_GATE;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) HV_TSS;

typedef struct HV_CPU {
    HV_VMCB *vmcb;                 /* +0x00 */
    uint8_t *host_stack_top;       /* +0x08 */
    HV_GPRS gprs;                  /* +0x10 */
    HV_VMCB *host_vmcb;            /* +0x80 */
    void *hsave;
    uint32_t processor_number;
    uint32_t apic_id;
    volatile uint32_t state;
    volatile uint32_t sipi_vector;
    uint64_t first_cr3;
    uint64_t vmexit_count;
    uint8_t fx_state[512] __attribute__((aligned(16)));
    uint8_t host_fx_state[512] __attribute__((aligned(16)));
    uint64_t apic_fault_address;
    uint32_t apic_shadowed;
    uint32_t discard_init;
    uint64_t last_exit_code;
    uint64_t last_exit_rip;
    uint64_t same_exit_count;
    uint32_t last_exit_msr;
    uint32_t last_exit_write;
    uint64_t host_cr3;
    void *host_gdt;
    HV_IDT_GATE *host_idt;
    HV_TSS *host_tss;
    DTR host_gdtr;
    DTR host_idtr;
    uint16_t host_tr;
    uint16_t host_cs;
    uint32_t sipi_count;
    uint32_t init_count;
    uint64_t sipi_deadline;
    uint64_t last_icr;
    volatile uint32_t host_context_active;
} HV_CPU;

typedef struct {
    EFI_SYSTEM_TABLE *st;
    EFI_HANDLE image;
    void *mp;
    UINTN cpu_count;
    UINTN enabled_cpu_count;
    UINTN bsp_number;
    HV_CPU *cpus;
    void *npt_root;
    void *npt_bsp_root;
    void *msrpm;
    HV_LOG_RING *log;
    EFI_HANDLE bootmgfw;
    volatile uint32_t virtualized_count;
    uint32_t x2apic;
    uint32_t boot_started;
    void *apic_shadow_page;
    EFI_EVENT exit_boot_event;
    EFI_EVENT virtual_address_event;
    volatile uint32_t sipi_remaining;
    volatile uint32_t xapic_intercept_active;
    volatile uint32_t exit_boot_seen;
    volatile uint32_t xapic_flush_pending;
    volatile uint32_t virtual_mode_seen;
    volatile uint32_t pre_ebs_logged;
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_stride;
    uint32_t framebuffer_bgr;
    void *host_cr3;
    volatile uint32_t apic_activation_pending;
    volatile uint32_t apic_started_count;
    volatile uint32_t stage_code;
    volatile uint32_t apic_npf_count;
    volatile uint32_t apic_db_count;
} HV_ROOT;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx;
} HV_EXIT_FRAME;

_Static_assert(sizeof(HV_VMCB_CONTROL) == 0x400, "VMCB control layout");
_Static_assert(offsetof(HV_VMCB_CONTROL, exit_code) == 0x70, "VMCB exit offset");
_Static_assert(offsetof(HV_VMCB_CONTROL, ncr3) == 0xb0, "VMCB NCR3 offset");
_Static_assert(offsetof(HV_VMCB_STATE, efer) == 0xd0, "VMCB EFER offset");
_Static_assert(offsetof(HV_VMCB_STATE, rip) == 0x178, "VMCB RIP offset");
_Static_assert(offsetof(HV_VMCB_STATE, rsp) == 0x1d8, "VMCB RSP offset");
_Static_assert(offsetof(HV_VMCB_STATE, rax) == 0x1f8, "VMCB RAX offset");
_Static_assert(sizeof(HV_VMCB) == 0x1000, "VMCB size");
_Static_assert(offsetof(HV_CPU, gprs) == 0x10, "assembly CPU layout");
_Static_assert(offsetof(HV_CPU, host_vmcb) == 0x80, "assembly host VMCB layout");
_Static_assert(offsetof(HV_CPU, fx_state) == 0xb0, "assembly FX layout");
_Static_assert(offsetof(HV_CPU, host_fx_state) == 0x2b0, "assembly host FX layout");
_Static_assert(offsetof(HV_CPU, host_cr3) > offsetof(HV_CPU, fx_state), "host context follows assembly state");
_Static_assert(sizeof(HV_EXIT_FRAME) == 0x70, "assembly exit frame size");
_Static_assert(offsetof(HV_EXIT_FRAME, r15) == 0x00, "assembly R15 offset");
_Static_assert(offsetof(HV_EXIT_FRAME, rdx) == 0x58, "assembly RDX offset");
_Static_assert(offsetof(HV_EXIT_FRAME, rbx) == 0x68, "assembly RBX offset");

int svm_enter_context_resume(HV_CPU *cpu);
HV_CPU *svm_prepare_cpu_context(HV_CPU *cpu, uint64_t guest_rsp, uint64_t guest_rip);
void svm_handle_guest_exit(HV_CPU *cpu, HV_EXIT_FRAME *frame);

#endif

/*
 * Transparent UEFI AMD-SVM context-resume loader.
 *
 * The context-resume and INIT/SIPI design follows HelloAmdHvPkg by Satoshi
 * Tanda (MIT). This is a clean GNU-EFI implementation adapted to this loader.
 */
#include <efiprot.h>

#define HV_LOG_MAGIC 0x3430474f4c564848ULL /* HHVLOG04 */
#define HV_LOG_VERSION 48
#define HV_EVT_CPU_PREPARE 1
#define HV_EVT_CPU_GUEST   2
#define HV_EVT_VMEXIT      3
#define HV_EVT_CR3         4
#define HV_EVT_INIT        5
#define HV_EVT_SIPI        6
#define HV_EVT_EXIT_BOOT   7
#define HV_EVT_APIC_ON     8
#define HV_EVT_APIC_OFF    9
#define HV_EVT_VIRTUAL_MODE 10
#define HV_EVT_HOST_CONTEXT 11
#define HV_EVT_AP_TIMEOUT 12
#define HV_EVT_STAGE 13
#define HV_EVT_FATAL       0xff
#define HV_CPU_RUNNING     1
#define HV_CPU_WAIT_INIT   2
#define HV_CPU_WAIT_SIPI   3
#define HV_CPU_APIC_STEP   4
#define HV_CPU_BOOTSTRAP   5
#define HV_CPU_STARTED     6
#define HV_CPU_SIPI_ISSUED 7
#define SVM_EXIT_DB        0x41ULL

#define HV_STAGE_EBS       0xE8B5000000000001ULL
#define HV_STAGE_VIRTUAL   0xE8B5000000000002ULL
#define HV_STAGE_APIC_ON   0xE8B5000000000003ULL
#define HV_STAGE_INIT      0xE8B5000000000004ULL
#define HV_STAGE_SIPI      0xE8B5000000000005ULL
#define HV_STAGE_APS_DONE  0xE8B5000000000006ULL
#define HV_STAGE_APIC_NPF  0xE8B5000000000010ULL
#define HV_STAGE_APIC_DB   0xE8B5000000000011ULL
#define HV_STAGE_NATIVE_APS 0xE8B5000000000020ULL
#define HV_STAGE_POST_EBS_HOST 0xE8B5000000000021ULL
#define HV_STAGE_NESTED_SVM_BLOCKED 0xE8B5000000000022ULL
#define HV_STAGE_HOST_FAULT 0xE8B5FFFFFFFFFFFFULL

extern void svm_host_exception_2(void);
extern void svm_host_exception_8(void);
extern void svm_host_exception_13(void);
extern void svm_host_exception_14(void);
extern void svm_host_exception_18(void);

#define EFI_MP_SERVICES_PROTOCOL_GUID \
    {0x3fdda605,0xa76e,0x4f46,{0xad,0x29,0x12,0xf4,0x53,0x1b,0x3d,0x08}}

typedef struct _EFI_MP_SERVICES_PROTOCOL EFI_MP_SERVICES_PROTOCOL;
typedef void (EFIAPI *EFI_AP_PROCEDURE)(void *);
typedef EFI_STATUS (EFIAPI *MP_GET_COUNT)(EFI_MP_SERVICES_PROTOCOL*,UINTN*,UINTN*);
typedef EFI_STATUS (EFIAPI *MP_GET_INFO)(EFI_MP_SERVICES_PROTOCOL*,UINTN,void*);
typedef EFI_STATUS (EFIAPI *MP_START_ALL)(EFI_MP_SERVICES_PROTOCOL*,EFI_AP_PROCEDURE,BOOLEAN,EFI_EVENT,UINTN,void*,UINTN**);
typedef EFI_STATUS (EFIAPI *MP_START_ONE)(EFI_MP_SERVICES_PROTOCOL*,EFI_AP_PROCEDURE,UINTN,EFI_EVENT,UINTN,void*,BOOLEAN*);
typedef EFI_STATUS (EFIAPI *MP_SWITCH_BSP)(EFI_MP_SERVICES_PROTOCOL*,UINTN,BOOLEAN);
typedef EFI_STATUS (EFIAPI *MP_ENABLE_AP)(EFI_MP_SERVICES_PROTOCOL*,UINTN,BOOLEAN,UINT32*);
typedef EFI_STATUS (EFIAPI *MP_WHOAMI)(EFI_MP_SERVICES_PROTOCOL*,UINTN*);

struct _EFI_MP_SERVICES_PROTOCOL {
    MP_GET_COUNT GetNumberOfProcessors;
    MP_GET_INFO GetProcessorInfo;
    MP_START_ALL StartupAllAPs;
    MP_START_ONE StartupThisAP;
    MP_SWITCH_BSP SwitchBSP;
    MP_ENABLE_AP EnableDisableAP;
    MP_WHOAMI WhoAmI;
};

static HV_ROOT g_root;
static uint64_t *g_apic_npt_pte;
static void framebuffer_stage(uint64_t stage,uint32_t rgb);
static void file_log_value(const char *label,uint64_t value);
static EFI_GUID g_mp_guid = EFI_MP_SERVICES_PROTOCOL_GUID;
static EFI_GUID g_sfs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL;
static EFI_GUID g_loaded_image_guid = LOADED_IMAGE_PROTOCOL;
static EFI_GUID g_log_guid = {0xb6646b40,0xe3f7,0x4b36,{0x98,0xa1,0x44,0x56,0x34,0x30,0x4c,0x47}};

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi; __asm__ __volatile__("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ __volatile__("wrmsr"::"c"(msr),"a"((uint32_t)v),"d"((uint32_t)(v>>32)):"memory");
}
static inline uint64_t read_cr0(void){uint64_t v;__asm__ __volatile__("mov %%cr0,%0":"=r"(v));return v;}
static inline uint64_t read_cr2(void){uint64_t v;__asm__ __volatile__("mov %%cr2,%0":"=r"(v));return v;}
static inline uint64_t read_cr3(void){uint64_t v;__asm__ __volatile__("mov %%cr3,%0":"=r"(v));return v;}
static inline uint64_t read_cr4(void){uint64_t v;__asm__ __volatile__("mov %%cr4,%0":"=r"(v));return v;}
static inline uint64_t read_flags(void){uint64_t v;__asm__ __volatile__("pushfq; pop %0":"=r"(v));return v;}
static inline uint64_t read_tsc(void){uint32_t lo,hi;__asm__ __volatile__("rdtsc":"=a"(lo),"=d"(hi));return ((uint64_t)hi<<32)|lo;}
static inline uint16_t read_cs(void){uint16_t v;__asm__ __volatile__("mov %%cs,%0":"=r"(v));return v;}
static inline uint16_t read_ss(void){uint16_t v;__asm__ __volatile__("mov %%ss,%0":"=r"(v));return v;}
static inline uint16_t read_ds(void){uint16_t v;__asm__ __volatile__("mov %%ds,%0":"=r"(v));return v;}
static inline uint16_t read_es(void){uint16_t v;__asm__ __volatile__("mov %%es,%0":"=r"(v));return v;}
static inline uint16_t read_fs(void){uint16_t v;__asm__ __volatile__("mov %%fs,%0":"=r"(v));return v;}
static inline uint16_t read_gs(void){uint16_t v;__asm__ __volatile__("mov %%gs,%0":"=r"(v));return v;}
static inline uint16_t read_tr(void){uint16_t v;__asm__ __volatile__("str %0":"=r"(v));return v;}
static inline void cpuid(uint32_t l,uint32_t s,uint32_t*a,uint32_t*b,uint32_t*c,uint32_t*d){__asm__ __volatile__("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(l),"c"(s));}

static void memzero(void *p, UINTN n) { uint8_t *q=p; while(n--) *q++=0; }

static void ring_log(HV_CPU *cpu,uint32_t event,uint64_t a,uint64_t b,uint64_t c,uint64_t d)
{
    HV_LOG_RING *r=g_root.log; uint64_t i;
    if(!r) return;
    i=__atomic_fetch_add(&r->write_index,1,__ATOMIC_RELAXED);
    HV_LOG_ENTRY *e=&r->entries[i%HV_LOG_ENTRIES];
    e->sequence=i; e->cpu=cpu?cpu->processor_number:0xffffffffU;
    e->event=event; e->a=a; e->b=b; e->c=c; e->d=d;
}

static EFI_STATUS alloc_pages(EFI_MEMORY_TYPE type,UINTN pages,void **out)
{
    EFI_PHYSICAL_ADDRESS a=0; EFI_STATUS s;
    s=g_root.st->BootServices->AllocatePages(AllocateAnyPages,type,pages,&a);
    if(EFI_ERROR(s)) return s;
    *out=(void*)(uintptr_t)a; memzero(*out,pages*4096); return EFI_SUCCESS;
}

static int make_host_page_tables(void **out_root)
{
    uint64_t *pml4,*pdpt;UINTN pml4i,pdpti;uint64_t phys;
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&pml4)))return 0;
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,4,(void**)&pdpt)))return 0;
    for(pml4i=0;pml4i<4;pml4i++){
        pml4[pml4i]=((uint64_t)(uintptr_t)(pdpt+pml4i*512))|3ULL;
        for(pdpti=0;pdpti<512;pdpti++){
            phys=(pml4i*512ULL+pdpti)<<30;
            pdpt[pml4i*512+pdpti]=phys|0x83ULL;
        }
    }
    *out_root=pml4;return 1;
}

static void set_idt_gate(HV_IDT_GATE *gate,void (*handler)(void),uint16_t selector)
{
    uint64_t address=(uint64_t)(uintptr_t)handler;
    gate->offset_low=(uint16_t)address;gate->selector=selector;gate->ist=0;
    gate->type_attr=0x8e;gate->offset_mid=(uint16_t)(address>>16);
    gate->offset_high=(uint32_t)(address>>32);gate->reserved=0;
}

static void set_tss_descriptor(uint8_t *gdt,uint16_t selector,HV_TSS *tss)
{
    uint8_t *d=gdt+(selector&~7U);uint64_t base=(uint64_t)(uintptr_t)tss;
    uint32_t limit=(uint32_t)sizeof(*tss)-1;
    memzero(d,16);d[0]=(uint8_t)limit;d[1]=(uint8_t)(limit>>8);
    d[2]=(uint8_t)base;d[3]=(uint8_t)(base>>8);d[4]=(uint8_t)(base>>16);
    d[5]=0x89;d[6]=(uint8_t)((limit>>16)&15);d[7]=(uint8_t)(base>>24);
    *(uint32_t*)(d+8)=(uint32_t)(base>>32);
}

static int prepare_host_context(HV_CPU *cpu)
{
    DTR current_gdtr;UINTN gdt_pages;uint8_t *gdt;HV_IDT_GATE *idt;HV_TSS *tss;
    uint16_t tr=read_tr(),cs=read_cs();
    __asm__ __volatile__("sgdt %0":"=m"(current_gdtr));
    if(cpu->processor_number==g_root.bsp_number){
        file_log_value("FIRMWARE GDTR LIMIT=",current_gdtr.limit);
        file_log_value("FIRMWARE TR=",tr);
    }
    /* Reserve one extra descriptor pair in case firmware has no active TR. */
    gdt_pages=((UINTN)current_gdtr.limit+1+16+4095)/4096;if(!gdt_pages)gdt_pages=1;
    if(gdt_pages>16){file_log_value("HOST GDT PAGES INVALID=",gdt_pages);return 0;}
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,gdt_pages,(void**)&gdt)))return 0;
    CopyMem(gdt,(void*)(uintptr_t)current_gdtr.base,(UINTN)current_gdtr.limit+1);
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&idt)))return 0;
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&tss)))return 0;
    tss->rsp0=(uint64_t)(uintptr_t)cpu->host_stack_top;tss->iomap_base=sizeof(*tss);
    /* A null firmware TR is valid in UEFI. Do not invent and load one. */
    if(tr&&(UINTN)(tr&~7U)+15>=gdt_pages*4096){file_log_value("HOST TSS SELECTOR FAILED=",tr);return 0;}
    if(cpu->processor_number==g_root.bsp_number)file_log_value("HOST TR SELECTOR=",tr);
    if(tr)set_tss_descriptor(gdt,tr,tss);
    for(UINTN i=0;i<256;i++)set_idt_gate(&idt[i],svm_host_exception_2,cs);
    set_idt_gate(&idt[2],svm_host_exception_2,cs);set_idt_gate(&idt[8],svm_host_exception_8,cs);
    set_idt_gate(&idt[13],svm_host_exception_13,cs);set_idt_gate(&idt[14],svm_host_exception_14,cs);
    set_idt_gate(&idt[18],svm_host_exception_18,cs);
    cpu->host_cr3=(uint64_t)(uintptr_t)g_root.host_cr3;cpu->host_gdt=gdt;cpu->host_idt=idt;
    cpu->host_tss=tss;cpu->host_tr=tr;cpu->host_cs=cs;
    cpu->host_gdtr.limit=(uint16_t)(gdt_pages*4096-1);cpu->host_gdtr.base=(uint64_t)(uintptr_t)gdt;
    cpu->host_idtr.limit=(uint16_t)(256*sizeof(*idt)-1);cpu->host_idtr.base=(uint64_t)(uintptr_t)idt;
    return 1;
}

static void activate_host_context(HV_CPU *cpu)
{
    __asm__ __volatile__("mov %0,%%cr3; lgdt %1; lidt %2"::
        "r"(cpu->host_cr3),"m"(cpu->host_gdtr),"m"(cpu->host_idtr):"memory");
    if(cpu->host_tr)__asm__ __volatile__("ltr %0"::"r"(cpu->host_tr):"memory");
}

static void file_log(const char *s)
{
    EFI_LOADED_IMAGE *li=NULL; EFI_FILE_IO_INTERFACE *fs=NULL; EFI_FILE_HANDLE root=NULL,f=NULL;
    UINTN n=0; const char *p=s;
    if(!g_root.st || !g_root.image) return;
    if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(g_root.image,&g_loaded_image_guid,(void**)&li))||!li)return;
    if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(li->DeviceHandle,&g_sfs_guid,(void**)&fs))||!fs)return;
    if(EFI_ERROR(fs->OpenVolume(fs,&root))||!root)return;
    if(!EFI_ERROR(root->Open(root,&f,L"\\HVBOOT.LOG",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0))&&f){
        f->SetPosition(f,~0ULL); while(p[n])n++; f->Write(f,&n,(void*)s); n=2; f->Write(f,&n,"\r\n"); f->Flush(f); f->Close(f);
    }
    root->Close(root);
}

static void file_log_value(const char *label,uint64_t value)
{
    char line[96],digits[16];UINTN n=0,i=0,j;const char *p=label;
    while(*p&&n+1<sizeof(line))line[n++]=*p++;
    if(n+3<sizeof(line)){line[n++]='0';line[n++]='x';}
    do{uint8_t d=(uint8_t)(value&15);digits[i++]=(char)(d<10?'0'+d:'A'+d-10);value>>=4;}while(value&&i<16);
    for(j=0;j<i&&n+1<sizeof(line);j++)line[n++]=digits[i-1-j];
    line[n]=0;file_log(line);
}

static UINTN line_text(char *line,UINTN n,UINTN cap,const char *text)
{
    while(*text&&n+1<cap)line[n++]=*text++;
    return n;
}

static UINTN line_hex(char *line,UINTN n,UINTN cap,uint64_t value)
{
    char digits[16];UINTN i=0,j;
    if(n+2<cap){line[n++]='0';line[n++]='x';}
    do{uint8_t d=(uint8_t)(value&15);digits[i++]=(char)(d<10?'0'+d:'A'+d-10);value>>=4;}while(value&&i<16);
    for(j=0;j<i&&n+1<cap;j++)line[n++]=digits[i-1-j];
    return n;
}

static void file_log_vmexit(HV_CPU *cpu,uint64_t code,uint64_t rip,uint32_t msr,
                            uint32_t write,uint64_t value,uint64_t nrip,uint64_t repeat)
{
    char line[256];UINTN n=0;
    n=line_text(line,n,sizeof(line),"VMEXIT cpu=");n=line_hex(line,n,sizeof(line),cpu->processor_number);
    n=line_text(line,n,sizeof(line)," code=");n=line_hex(line,n,sizeof(line),code);
    n=line_text(line,n,sizeof(line)," rip=");n=line_hex(line,n,sizeof(line),rip);
    if(code==SVM_EXIT_MSR){
        n=line_text(line,n,sizeof(line),write?" wrmsr=":" rdmsr=");n=line_hex(line,n,sizeof(line),msr);
        n=line_text(line,n,sizeof(line)," value=");n=line_hex(line,n,sizeof(line),value);
    }
    n=line_text(line,n,sizeof(line)," nrip=");n=line_hex(line,n,sizeof(line),nrip);
    n=line_text(line,n,sizeof(line)," repeat=");n=line_hex(line,n,sizeof(line),repeat);
    line[n]=0;file_log(line);
}

static void file_write_blob(CHAR16 *name,void *data,UINTN size)
{
    EFI_LOADED_IMAGE *li=NULL;EFI_FILE_IO_INTERFACE *fs=NULL;EFI_FILE_HANDLE root=NULL,f=NULL;
    if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(g_root.image,&g_loaded_image_guid,(void**)&li))||!li)return;
    if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(li->DeviceHandle,&g_sfs_guid,(void**)&fs))||!fs)return;
    if(EFI_ERROR(fs->OpenVolume(fs,&root))||!root)return;
    if(!EFI_ERROR(root->Open(root,&f,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0))&&f){
        f->SetPosition(f,0);f->Write(f,&size,data);f->Flush(f);f->Close(f);
    }
    root->Close(root);
}

static int address_is_preserved_nvs(uint64_t address,UINTN bytes)
{
    EFI_MEMORY_DESCRIPTOR *map=NULL,*d;UINTN size=0,key=0,ds=0;UINT32 ver=0;EFI_STATUS s;UINTN off;
    s=g_root.st->BootServices->GetMemoryMap(&size,map,&key,&ds,&ver);
    if(s!=EFI_BUFFER_TOO_SMALL||ds==0)return 0;
    size+=ds*4;if(EFI_ERROR(g_root.st->BootServices->AllocatePool(EfiLoaderData,size,(void**)&map)))return 0;
    s=g_root.st->BootServices->GetMemoryMap(&size,map,&key,&ds,&ver);
    if(EFI_ERROR(s)){g_root.st->BootServices->FreePool(map);return 0;}
    for(off=0;off<size;off+=ds){d=(EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map+off);
        if(d->Type==EfiACPIMemoryNVS && address>=d->PhysicalStart &&
           address+bytes<=d->PhysicalStart+d->NumberOfPages*4096){g_root.st->BootServices->FreePool(map);return 1;}}
    g_root.st->BootServices->FreePool(map);return 0;
}

static void export_previous_ring(void)
{
    uint64_t address=0;UINTN size=sizeof(address);UINT32 attrs=0;HV_LOG_RING *r;
    if(EFI_ERROR(g_root.st->RuntimeServices->GetVariable(L"HvTransparentSvmLogAddress",&g_log_guid,&attrs,&size,&address)))return;
    if(address_is_preserved_nvs(address,sizeof(HV_LOG_RING))){r=(HV_LOG_RING*)(uintptr_t)address;
        if(r->magic==HV_LOG_MAGIC && r->version>=40&&r->version<=HV_LOG_VERSION)
            file_write_blob(L"\\HVBOOT.PREV.BIN",r,sizeof(*r));}
    g_root.st->RuntimeServices->SetVariable(L"HvTransparentSvmLogAddress",&g_log_guid,0,0,NULL);
}

static void log_hex_line(const CHAR16 *label,uint64_t v)
{
    Print(L"%s: 0x%lx\n",label,v);
}

static void set_segment(HV_SEGMENT *s,uint16_t sel,DTR *gdt,uint16_t fallback)
{
    uint8_t *d; uint64_t base; uint32_t limit; uint16_t attr;
    memzero(s,sizeof(*s)); s->selector=sel;
    if((sel&~7U)>gdt->limit){s->attrib=fallback;return;}
    d=(uint8_t*)(uintptr_t)(gdt->base+(sel&~7U));
    limit=*(uint16_t*)d|((uint32_t)(d[6]&15)<<16);
    base=(uint64_t)*(uint16_t*)(d+2)|((uint64_t)d[4]<<16)|((uint64_t)d[7]<<24);
    attr=(uint16_t)d[5]|((uint16_t)(d[6]&0xf0)<<4);
    if((d[5]&0x10)==0 && (sel&~7U)+15<=gdt->limit) base|=(uint64_t)*(uint32_t*)(d+8)<<32;
    if(d[6]&0x80) limit=(limit<<12)|0xfff;
    s->base=base;s->limit=limit;s->attrib=attr;
}

static void msrpm_set(uint32_t msr,int read,int write)
{
    uint32_t off,idx; uint8_t *m=g_root.msrpm;
    if(msr<=0x1fff) off=0;
    else if(msr>=0xc0000000 && msr<=0xc0001fff){off=0x800;msr-=0xc0000000;}
    else if(msr>=0xc0010000 && msr<=0xc0011fff){off=0x1000;msr-=0xc0010000;}
    else return;
    idx=msr*2; if(read)m[off+(idx>>3)]|=1U<<(idx&7); idx++;
    if(write)m[off+(idx>>3)]|=1U<<(idx&7);
}

static int make_npt(int trap_apic,void **out_root,uint64_t **out_apic_pte)
{
    uint64_t *pml4,*pdpt,*pd,*pt; UINTN pml4i,pdpti,i; uint64_t phys;
    const uint64_t apic=0xfee00000ULL;
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&pml4)))return 0;
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,4,(void**)&pdpt)))return 0;
    for(pml4i=0;pml4i<4;pml4i++)pml4[pml4i]=((uint64_t)(uintptr_t)(pdpt+pml4i*512))|7;
    for(pml4i=0;pml4i<4;pml4i++)for(pdpti=0;pdpti<512;pdpti++){
        phys=((pml4i*512ULL+pdpti)<<30); pdpt[pml4i*512+pdpti]=phys|0x87;
    }
    /* Split the 1-GiB entry containing xAPIC, then its containing 2-MiB PDE. */
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&pd)))return 0;
    phys=apic&~((1ULL<<30)-1);
    for(i=0;i<512;i++)pd[i]=(phys+(i<<21))|0x87;
    pdpt[(apic>>30)&0x7ff]=(uint64_t)(uintptr_t)pd|7;
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&pt)))return 0;
    phys=apic&~((1ULL<<21)-1);
    for(i=0;i<512;i++)pt[i]=(phys+(i<<12))|7;
    pd[(apic>>21)&0x1ff]=(uint64_t)(uintptr_t)pt|7;
    if(out_apic_pte)*out_apic_pte=&pt[(apic>>12)&0x1ff];
    if(trap_apic && out_apic_pte)**out_apic_pte&=~2ULL;
    *out_root=pml4; return 1;
}

static uint32_t current_apic_id(void)
{
    uint32_t a,b,c,d,max;
    cpuid(0,0,&max,&b,&c,&d);
    if(max>=0x1f){cpuid(0x1f,0,&a,&b,&c,&d);if(b)return d;}
    if(max>=0x0b){cpuid(0x0b,0,&a,&b,&c,&d);if(b)return d;}
    cpuid(1,0,&a,&b,&c,&d);return b>>24;
}

#define XAPIC_BASE       0xfee00000ULL
#define XAPIC_ICR_LOW    0x300U
#define XAPIC_ICR_HIGH   0x310U


static void xapic_set_mapping(uint64_t page, int writable)
{
    HV_CPU *bsp;

    if (g_apic_npt_pte == NULL)
    {
        return;
    }

    *g_apic_npt_pte = (page & ~0xfffULL) | (writable ? 7ULL : 5ULL);

    __atomic_store_n(
        &g_root.xapic_flush_pending,
        1,
        __ATOMIC_SEQ_CST
    );

    bsp = &g_root.cpus[g_root.bsp_number];
    bsp->vmcb->control.tlb_control = 1;
    bsp->vmcb->control.vmcb_clean = 0;
}


static void xapic_set_intercept(int enable)
{
    if (g_apic_npt_pte == NULL || g_root.x2apic)
    {
        return;
    }

    g_root.xapic_intercept_active = enable ? 1U : 0U;

    xapic_set_mapping(
        XAPIC_BASE,
        enable ? 0 : 1
    );

    ring_log(
        &g_root.cpus[g_root.bsp_number],
        enable ? HV_EVT_APIC_ON : HV_EVT_APIC_OFF,
        g_root.sipi_remaining,
        0,
        0,
        0
    );
}


static int icr_targets_cpu(
    HV_CPU *source,
    HV_CPU *target,
    uint32_t destination,
    uint32_t shorthand
)
{
    if (shorthand == 1)
    {
        return target == source;
    }

    if (shorthand == 2)
    {
        return 1;
    }

    if (shorthand == 3)
    {
        return target != source;
    }

    return target->apic_id == destination;
}

static void handle_icr(HV_CPU *cpu,uint64_t icr)
{
    uint32_t mode=(uint32_t)((icr>>8)&7),vector=(uint8_t)icr;
    uint32_t shorthand=(uint32_t)((icr>>18)&3);
    uint32_t dest=g_root.x2apic?(uint32_t)(icr>>32):(uint32_t)(icr>>56);
    UINTN i;uint32_t selected=0;

    cpu->last_icr=icr;
    if(mode==5||mode==6){
        for(i=0;i<g_root.cpu_count;i++){
            HV_CPU *target=&g_root.cpus[i];
            if(!icr_targets_cpu(cpu,target,dest,shorthand))continue;
            if(target==cpu)continue;
            selected++;
            if(mode==5){
                ring_log(cpu,HV_EVT_INIT,target->apic_id,icr,shorthand,target->state);
                framebuffer_stage(HV_STAGE_INIT,0x00cc8800U);
                target->init_count++;
                if(target->state!=HV_CPU_STARTED){
                    target->sipi_vector=0;target->state=HV_CPU_WAIT_SIPI;
                    target->discard_init=0;target->sipi_deadline=read_tsc()+15000000000ULL;
                }
            }else{
                ring_log(cpu,HV_EVT_SIPI,target->apic_id,vector,shorthand,g_root.sipi_remaining);
                framebuffer_stage(HV_STAGE_SIPI,0x000088ccU);
                if(target->sipi_count<2)target->sipi_count++;
                /* SIPI can race ahead of the target's #SX VMEXIT. Preserve it. */
                if(target->state!=HV_CPU_STARTED){
                    target->sipi_vector=vector;target->state=HV_CPU_SIPI_ISSUED;
                }
                if(g_root.sipi_remaining)__atomic_fetch_sub(&g_root.sipi_remaining,1,__ATOMIC_SEQ_CST);
            }
        }
    }

    /* Match HelloAmdHvPkg: emulate first, then forward the original ICR write. */
    if(g_root.x2apic)wrmsr(MSR_X2APIC_ICR,icr);
    else ((volatile uint32_t*)(uintptr_t)XAPIC_BASE)[XAPIC_ICR_LOW/4]=(uint32_t)icr;

    if(mode==6&&selected&&g_root.sipi_remaining==0)
        g_root.apic_activation_pending=2;
}

static void reset_for_sipi(HV_CPU *cpu,uint8_t vector)
{
    HV_VMCB_STATE *s=&cpu->vmcb->state; uint64_t base=(uint64_t)vector<<12;
    uint64_t old_cr0=s->cr0;uint32_t a,b,c,d;
    cpuid(1,0,&a,&b,&c,&d);
    memzero(&cpu->gprs,sizeof(cpu->gprs));
    memzero(s,sizeof(*s));
    s->cs.selector=(uint16_t)vector<<8;s->cs.base=base;s->cs.limit=0xffff;s->cs.attrib=0x9b;
    s->ds.limit=s->es.limit=s->ss.limit=s->fs.limit=s->gs.limit=0xffff;
    s->ds.attrib=s->es.attrib=s->ss.attrib=s->fs.attrib=s->gs.attrib=0x93;
    s->gdtr.limit=s->idtr.limit=0xffff;s->ldtr.limit=s->tr.limit=0xffff;
    s->ldtr.attrib=0x82;s->tr.attrib=0x8b;
    s->cr0=0x10|(old_cr0&((1ULL<<30)|(1ULL<<29)));s->cr2=0;s->cr3=0;s->cr4=0;
    s->dr6=0xffff0ff0;s->dr7=0x400;s->rflags=2;s->rip=0;s->rsp=0;
    s->efer=EFER_SVME;s->gpat=rdmsr(MSR_PAT);cpu->gprs.rdx=a;
    cpu->vmcb->control.tlb_control=1;cpu->vmcb->control.vmcb_clean=0;
}

static const uint8_t g_hex_font[16][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16}
};

static void framebuffer_hex_line(uint32_t y,uint64_t value,uint32_t color)
{
    uint32_t *fb=(uint32_t*)(uintptr_t)g_root.framebuffer_base;uint32_t n,row,col,scale=2,x=20;
    if(!fb||g_root.framebuffer_width<220||y+14>=g_root.framebuffer_height)return;
    for(n=0;n<16;n++){
        uint8_t digit=(uint8_t)((value>>((15-n)*4))&15);
        for(row=0;row<7;row++)for(col=0;col<5;col++)if(g_hex_font[digit][row]&(1U<<(4-col))){
            uint32_t px=x+n*12+col*scale,py=y+row*scale,dy,dx;
            for(dy=0;dy<scale;dy++)for(dx=0;dx<scale;dx++)fb[(py+dy)*g_root.framebuffer_stride+px+dx]=color;
        }
    }
}

static void framebuffer_stage(uint64_t stage,uint32_t rgb)
{
    uint32_t *fb=(uint32_t*)(uintptr_t)g_root.framebuffer_base;uint32_t color,x,y;
    if(!fb||!g_root.framebuffer_stride||g_root.framebuffer_width<220)return;
    color=g_root.framebuffer_bgr?((rgb&0xff)<<16)|(rgb&0xff00)|((rgb>>16)&0xff):rgb;
    for(y=0;y<22&&y<g_root.framebuffer_height;y++)
        for(x=0;x<220&&x<g_root.framebuffer_width;x++)fb[y*g_root.framebuffer_stride+x]=color;
    framebuffer_hex_line(4,stage,0x00ffffffU);g_root.stage_code=(uint32_t)stage;
}

void svm_host_exception_fatal(uint64_t vector,uint64_t error,uint64_t rip,uint64_t cr2)
{
    HV_CPU *cpu=g_root.cpus?&g_root.cpus[g_root.bsp_number]:NULL;
    if(cpu)ring_log(cpu,HV_EVT_FATAL,0x1000|vector,rip,error,cr2);
    framebuffer_stage(HV_STAGE_HOST_FAULT,0x00cc0000U);
    framebuffer_hex_line(24,vector,0x00ffffffU);framebuffer_hex_line(44,rip,0x00ffffffU);
    framebuffer_hex_line(64,error,0x00ffffffU);framebuffer_hex_line(84,cr2,0x00ffffffU);
    for(;;)__asm__ __volatile__("cli; hlt");
}

static void framebuffer_fatal(HV_CPU *cpu,uint64_t code)
{
    uint32_t *fb=(uint32_t*)(uintptr_t)g_root.framebuffer_base;uint32_t red,white,x,y,maxx,maxy;
    HV_VMCB_STATE *s=&cpu->vmcb->state;
    if(!fb||!g_root.framebuffer_stride)return;
    red=g_root.framebuffer_bgr?0x00ff0000U:0x000000ffU;white=0x00ffffffU;
    maxx=g_root.framebuffer_width<230?g_root.framebuffer_width:230;
    maxy=g_root.framebuffer_height<180?g_root.framebuffer_height:180;
    for(y=0;y<maxy;y++)for(x=0;x<maxx;x++)fb[y*g_root.framebuffer_stride+x]=red;
    framebuffer_hex_line(4,code,white);
    framebuffer_hex_line(24,s->rip,white);
    framebuffer_hex_line(44,s->rsp,white);
    framebuffer_hex_line(64,s->cr3,white);
    framebuffer_hex_line(84,s->cr0,white);
    framebuffer_hex_line(104,s->cr4,white);
    framebuffer_hex_line(124,s->efer,white);
    framebuffer_hex_line(144,s->rflags,white);
}

static void framebuffer_stall(HV_CPU *cpu,uint64_t code,uint32_t msr,uint64_t nrip)
{
    uint32_t *fb=(uint32_t*)(uintptr_t)g_root.framebuffer_base;uint32_t red,white,x,y,maxx,maxy;
    if(!fb||!g_root.framebuffer_stride)return;
    red=g_root.framebuffer_bgr?0x00ff0000U:0x000000ffU;white=0x00ffffffU;
    maxx=g_root.framebuffer_width<230?g_root.framebuffer_width:230;
    maxy=g_root.framebuffer_height<100?g_root.framebuffer_height:100;
    for(y=0;y<maxy;y++)for(x=0;x<maxx;x++)fb[y*g_root.framebuffer_stride+x]=red;
    framebuffer_hex_line(4,code,white);
    framebuffer_hex_line(24,cpu->vmcb->state.rip,white);
    framebuffer_hex_line(44,msr,white);
    framebuffer_hex_line(64,nrip,white);
}

static void fatal(HV_CPU *cpu,uint64_t code)
{
    HV_VMCB *v=cpu->vmcb;
    ring_log(cpu,HV_EVT_FATAL,code,v->state.rip,v->state.rsp,v->state.rflags);
    ring_log(cpu,HV_EVT_FATAL,v->state.cr3,v->control.exit_info1,v->control.exit_info2,v->control.exit_int_info);
    if(!g_root.boot_started){
        file_log_value("EARLY FATAL VMEXIT=",code);
        file_log_value("EARLY FATAL RIP=",v->state.rip);
        file_log_value("EARLY FATAL RSP=",v->state.rsp);
        file_log_value("EARLY FATAL INFO1=",v->control.exit_info1);
        file_log_value("EARLY FATAL INFO2=",v->control.exit_info2);
    }
    framebuffer_fatal(cpu,code);
    if(!g_root.exit_boot_seen){
        file_log_value("FATAL VMEXIT=",code);
        file_log_value("FATAL RIP=",v->state.rip);
        file_log_value("FATAL INFO1=",v->control.exit_info1);
        file_log_value("FATAL INFO2=",v->control.exit_info2);
        file_log_value("FATAL RSP=",v->state.rsp);
    }
    for(;;)__asm__ __volatile__("cli; hlt");
}

static void enable_runtime_intercepts(HV_CPU *cpu)
{
    HV_VMCB_CONTROL *c=&cpu->vmcb->control;
    /* CPUID gives the host a safe rendezvous after SetVirtualAddressMap. */
    c->intercept_misc1=SVM_INTERCEPT_CPUID|SVM_INTERCEPT_MSR|SVM_INTERCEPT_SHUTDOWN;
    c->intercept_misc2=SVM_INTERCEPT_VMRUN|SVM_INTERCEPT_VMMCALL;
    c->intercept_exception=(1U<<30); /* #SX / redirected INIT. */
    c->vmcb_clean=0;
    cpu->state=HV_CPU_RUNNING;
}

void svm_handle_guest_exit(HV_CPU *cpu,HV_EXIT_FRAME *f)
{
    HV_VMCB *v=cpu->vmcb; uint64_t code=v->control.exit_code;
    uint32_t sig_msr=(code==SVM_EXIT_MSR)?(uint32_t)f->rcx:0;
    uint32_t sig_write=(code==SVM_EXIT_MSR)?(uint32_t)(v->control.exit_info1&1):0;
    uint64_t sig_value=(f->rdx<<32)|(uint32_t)v->state.rax;
    int signature_changed;
    cpu->vmexit_count++; v->control.event_inj=0;
    v->control.tlb_control=0;
    if(g_root.exit_boot_seen){
        v->control.intercept_misc1|=SVM_INTERCEPT_CPUID;
        v->control.vmcb_clean=0;
    }
    /*
     * Switch to the private host CR3/GDT/IDT at the first VMEXIT after
     * ExitBootServices. Waiting for the virtual-address-change callback is
     * fragile because that callback itself executes from this runtime image.
     */
    if(g_root.exit_boot_seen&&!cpu->host_context_active){
        activate_host_context(cpu);
        __asm__ __volatile__("vmsave %0"::"a"((uint64_t)(uintptr_t)cpu->host_vmcb):"memory");
        cpu->host_context_active=1;
        g_root.virtual_mode_seen=1;
        ring_log(cpu,HV_EVT_HOST_CONTEXT,cpu->host_cr3,cpu->host_gdtr.base,cpu->host_idtr.base,cpu->host_tr);
        if(cpu->processor_number==g_root.bsp_number)
            framebuffer_stage(HV_STAGE_POST_EBS_HOST,0x0000aa88U);
    }
    if(cpu->processor_number==g_root.bsp_number&&g_root.virtual_mode_seen){
        uint32_t action=__atomic_exchange_n(&g_root.apic_activation_pending,0,__ATOMIC_SEQ_CST);
        if(action==1&&!g_root.x2apic&&!g_root.xapic_intercept_active){
            framebuffer_stage(HV_STAGE_VIRTUAL,0x00550055U);
            xapic_set_intercept(1);framebuffer_stage(HV_STAGE_APIC_ON,0x000000ccU);
        }else if(action==2&&g_root.xapic_intercept_active){
            framebuffer_stage(HV_STAGE_APS_DONE,0x0000aa44U);xapic_set_intercept(0);
        }
    }
    if(cpu->processor_number==g_root.bsp_number&&
       __atomic_exchange_n(&g_root.xapic_flush_pending,0,__ATOMIC_SEQ_CST))
        v->control.tlb_control=1;
    signature_changed=(cpu->last_exit_code!=code||cpu->last_exit_rip!=v->state.rip||
                       cpu->last_exit_msr!=sig_msr||cpu->last_exit_write!=sig_write);
    if(signature_changed){
        cpu->last_exit_code=code;cpu->last_exit_rip=v->state.rip;
        cpu->last_exit_msr=sig_msr;cpu->last_exit_write=sig_write;cpu->same_exit_count=1;
    }else cpu->same_exit_count++;
    if(cpu->vmexit_count<=32||signature_changed||
       (cpu->same_exit_count&&(cpu->same_exit_count&(cpu->same_exit_count-1))==0)){
        ring_log(cpu,HV_EVT_VMEXIT,code,v->state.rip,sig_msr,cpu->same_exit_count);
        if(g_root.boot_started&&!g_root.exit_boot_seen&&g_root.pre_ebs_logged<64){
            __atomic_fetch_add(&g_root.pre_ebs_logged,1,__ATOMIC_RELAXED);
            file_log_vmexit(cpu,code,v->state.rip,sig_msr,sig_write,sig_value,
                            v->control.nrip,cpu->same_exit_count);
        }
    }
    if(0&&cpu->same_exit_count==0xFFFFFFFFU){
        ring_log(cpu,HV_EVT_FATAL,code,v->state.rip,sig_msr,v->control.nrip);
        if(!g_root.exit_boot_seen)file_log_vmexit(cpu,code,v->state.rip,sig_msr,sig_write,
                                                  sig_value,v->control.nrip,cpu->same_exit_count);
        framebuffer_stall(cpu,code,sig_msr,v->control.nrip);
        for(;;)__asm__ __volatile__("cli; hlt");
    }
    if(code==SVM_EXIT_CPUID){
        uint32_t a,b,c,d,leaf=(uint32_t)v->state.rax,sub=(uint32_t)f->rcx;
        /*
         * During the UEFI phase, allow the old escape from the firmware CPUID
         * polling loop. After ExitBootServices, keep CPUID intercepted so
         * Windows can never rediscover SVM on the virtualized BSP.
         */
        if(cpu->same_exit_count>8&&!g_root.exit_boot_seen){
            v->control.intercept_misc1 &= ~SVM_INTERCEPT_CPUID;
            v->control.vmcb_clean=0;
        }
        cpuid(leaf,sub,&a,&b,&c,&d);

        /* Keep nested virtualization hidden from Windows. */
        if (leaf == 1)
        {
            c &= ~(1U << 5);

            if (HV_VIRTUALIZE_ALL_CPUS)
            {
                c |= 1U << 31;
            }
        }

        if (leaf == 0x80000001)
        {
            c &= ~(1U << 2);
        }

        if (leaf == 0x8000000A)
        {
            a = 0;
            b = 0;
            c = 0;
            d = 0;
        }

        if (HV_VIRTUALIZE_ALL_CPUS && leaf == 0x40000000)
        {
            a = 0x40000000;
            b = 0x48564300;
            c = 0;
            d = 0;
        }

        v->state.rax=a;f->rbx=b;f->rcx=c;f->rdx=d;
        v->state.rip=v->control.nrip?v->control.nrip:v->state.rip+2;
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_MSR){
        uint32_t msr=(uint32_t)f->rcx; int write=(v->control.exit_info1&1)!=0; uint64_t val;
        if(write){
            val=(f->rdx<<32)|(uint32_t)v->state.rax;
            if(msr==MSR_X2APIC_ICR)handle_icr(cpu,val);
            else if(msr==MSR_EFER){
                /*
                 * Keep SVME set in the hardware VMCB because AMD requires it
                 * for a valid guest state. The guest-visible RDMSR path below
                 * still returns SVME=0, so nested SVM remains hidden.
                 */
                v->state.efer=(val&~EFER_SVME)|EFER_SVME;
            }
            else if(msr==SVM_MSR_VM_CR||msr==SVM_MSR_HSAVE_PA){/* virtualized */}
            else wrmsr(msr,val);
        }else{
            if(msr==MSR_EFER)val=v->state.efer&~EFER_SVME;
            else if(msr==SVM_MSR_VM_CR)val=(rdmsr(msr)&~VM_CR_R_INIT)|(1ULL<<4);
            else if(msr==SVM_MSR_HSAVE_PA)val=0;
            else val=rdmsr(msr); /* passthrough */
            v->state.rax=(uint32_t)val;f->rdx=val>>32;
        }
        v->state.rip=v->control.nrip?v->control.nrip:v->state.rip+2;
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_VMMCALL){
        v->state.rip=v->control.nrip?v->control.nrip:v->state.rip+3;
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_SX){
        ring_log(cpu,HV_EVT_INIT,v->state.rip,cpu->discard_init,cpu->state,cpu->sipi_vector);
        if(!cpu->discard_init){
            uint32_t vector;
            cpu->state=HV_CPU_WAIT_SIPI;
            while((vector=cpu->sipi_vector)==0){
                if(cpu->sipi_deadline&&read_tsc()>cpu->sipi_deadline){
                    ring_log(cpu,HV_EVT_AP_TIMEOUT,cpu->apic_id,cpu->last_icr,cpu->state,cpu->sipi_count);
                    framebuffer_stage(HV_STAGE_HOST_FAULT,0x00cc0000U);
                    framebuffer_hex_line(24,cpu->apic_id,0x00ffffffU);
                    framebuffer_hex_line(44,cpu->last_icr,0x00ffffffU);
                    for(;;)__asm__ __volatile__("cli; hlt");
                }
                __asm__ __volatile__("pause");
            }
            reset_for_sipi(cpu,(uint8_t)vector);cpu->sipi_vector=0;
            cpu->state=HV_CPU_STARTED;cpu->discard_init=1;
            __atomic_fetch_add(&g_root.apic_started_count,1,__ATOMIC_SEQ_CST);
            if(g_root.apic_started_count+1>=g_root.enabled_cpu_count)g_root.apic_activation_pending=2;
        }
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_NPF && g_root.xapic_intercept_active && !g_root.x2apic &&
              (v->control.exit_info2&~0xfffULL)==0xfee00000ULL){
        uint32_t off=(uint32_t)(v->control.exit_info2&0xfffULL);
        if(__atomic_fetch_add(&g_root.apic_npf_count,1,__ATOMIC_RELAXED)==0){
            framebuffer_stage(HV_STAGE_APIC_NPF,0x000066aaU);
            framebuffer_hex_line(24,v->control.exit_info2,0x00ffffffU);
            framebuffer_hex_line(44,v->state.rip,0x00ffffffU);
        }
        cpu->apic_fault_address=v->control.exit_info2;
        cpu->apic_shadowed=(off==XAPIC_ICR_LOW);
        if(cpu->apic_shadowed){
            volatile uint32_t *apic=(volatile uint32_t*)(uintptr_t)XAPIC_BASE;
            uint32_t *shadow=(uint32_t*)g_root.apic_shadow_page;
            shadow[XAPIC_ICR_LOW/4]=apic[XAPIC_ICR_LOW/4];
            shadow[XAPIC_ICR_HIGH/4]=apic[XAPIC_ICR_HIGH/4];
            xapic_set_mapping((uint64_t)(uintptr_t)g_root.apic_shadow_page,1);
        }else xapic_set_mapping(XAPIC_BASE,1);
        cpu->state=HV_CPU_APIC_STEP;
        v->state.rflags|=(1ULL<<8);v->control.intercept_exception|=(1U<<1);
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_DB && cpu->state==HV_CPU_APIC_STEP){
        volatile uint32_t *apic=(volatile uint32_t*)(uintptr_t)0xfee00000ULL;
        uint64_t icr=0;
        if(__atomic_fetch_add(&g_root.apic_db_count,1,__ATOMIC_RELAXED)==0){
            framebuffer_stage(HV_STAGE_APIC_DB,0x00004488U);
            framebuffer_hex_line(24,cpu->apic_fault_address,0x00ffffffU);
            framebuffer_hex_line(44,v->state.rip,0x00ffffffU);
        }
        if(cpu->apic_shadowed){uint32_t *shadow=(uint32_t*)g_root.apic_shadow_page;
            icr=((uint64_t)apic[XAPIC_ICR_HIGH/4]<<32)|shadow[XAPIC_ICR_LOW/4];}
        xapic_set_mapping(XAPIC_BASE,g_root.xapic_intercept_active?0:1);
        cpu->state=HV_CPU_RUNNING;
        v->state.rflags&=~(1ULL<<8);v->control.intercept_exception&=~(1U<<1);
        v->control.vmcb_clean=0;
        if(cpu->apic_shadowed)handle_icr(cpu,icr);
        cpu->apic_shadowed=0;cpu->apic_fault_address=0;
    }else if(code==0x41ULL){
        /* #DB: single-step after APIC write - clear TF and continue */
        v->state.rflags&=~(1ULL<<8);v->control.intercept_exception&=~(1U<<1);
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_NPF){
        if(!g_root.exit_boot_seen){
            file_log_value("NPF ADDR=",v->control.exit_info2);
            file_log_value("NPF RIP=",v->state.rip);
        }
        if(cpu->same_exit_count>8)fatal(cpu,code);
    }else if(code==SVM_EXIT_VMRUN){
        /*
         * The hardware VMCB must keep EFER.SVME=1, but the guest sees
         * EFER.SVME=0 through the trapped RDMSR path. If guest code still
         * executes VMRUN, emulate the architectural result of SVME=0: #UD.
         */
        framebuffer_stage(HV_STAGE_NESTED_SVM_BLOCKED,0x00aa4400U);
        v->control.event_inj=(1ULL<<31)|(3ULL<<8)|6ULL;
        v->control.vmcb_clean=0;
    }else if(code==SVM_EXIT_SHUTDOWN)fatal(cpu,code);
    else{
        /* Unknown VMEXIT: log and fatal */
        file_log_value("UNKNOWN VMEXIT=",code);
        file_log_value("UNKNOWN RIP=",v->state.rip);
        file_log_value("UNKNOWN INFO1=",v->control.exit_info1);
        fatal(cpu,code);
    }

    cpu->gprs.rbx=f->rbx;cpu->gprs.rcx=f->rcx;cpu->gprs.rdx=f->rdx;cpu->gprs.rbp=f->rbp;
    cpu->gprs.rsi=f->rsi;cpu->gprs.rdi=f->rdi;cpu->gprs.r8=f->r8;cpu->gprs.r9=f->r9;
    cpu->gprs.r10=f->r10;cpu->gprs.r11=f->r11;cpu->gprs.r12=f->r12;cpu->gprs.r13=f->r13;
    cpu->gprs.r14=f->r14;cpu->gprs.r15=f->r15;
}

HV_CPU *svm_prepare_cpu_context(HV_CPU *cpu,uint64_t guest_rsp,uint64_t guest_rip)
{
    HV_VMCB *v=cpu->vmcb; DTR gdtr,idtr; uint32_t a,b,c,d;
    int bsp=(cpu->processor_number==g_root.bsp_number);
    /* SVM must be enabled before capturing the state that becomes the guest. */
    wrmsr(MSR_EFER,rdmsr(MSR_EFER)|EFER_SVME);
    wrmsr(SVM_MSR_VM_CR,rdmsr(SVM_MSR_VM_CR)|VM_CR_R_INIT);
    if(bsp)file_log("BSP SVM ENABLED");
    memzero(v,sizeof(*v));
    __asm__ __volatile__("sgdt %0":"=m"(gdtr));__asm__ __volatile__("sidt %0":"=m"(idtr));
    v->control.intercept_exception=(1U<<30); /* Redirected INIT arrives as #SX. */
    /* CR3 remains native. Intercepting it without full MOV-CR decoding loops. */
    v->control.intercept_cr_write=0;
    /* AMD requires VMRUN interception for every valid guest VMCB. */
    v->control.intercept_misc1=0;
    v->control.intercept_misc2=SVM_INTERCEPT_VMRUN;
    /* Keep physical interrupt delivery transparent during UEFI boot. */
    v->control.guest_asid=1;v->control.tlb_control=1;v->control.v_intr=0;
    v->control.msrpm_base_pa=(uint64_t)(uintptr_t)g_root.msrpm;
    v->control.np_enable=1;
    v->control.ncr3=(uint64_t)(uintptr_t)(cpu->processor_number==g_root.bsp_number?
        g_root.npt_bsp_root:g_root.npt_root);
    set_segment(&v->state.cs,read_cs(),&gdtr,0xa9b);set_segment(&v->state.ss,read_ss(),&gdtr,0x93);
    set_segment(&v->state.ds,read_ds(),&gdtr,0x93);set_segment(&v->state.es,read_es(),&gdtr,0x93);
    set_segment(&v->state.fs,read_fs(),&gdtr,0x93);set_segment(&v->state.gs,read_gs(),&gdtr,0x93);
    set_segment(&v->state.tr,read_tr(),&gdtr,0x8b);
    v->state.fs.base=rdmsr(0xc0000100);v->state.gs.base=rdmsr(0xc0000101);
    v->state.gdtr.base=gdtr.base;v->state.gdtr.limit=gdtr.limit;v->state.idtr.base=idtr.base;v->state.idtr.limit=idtr.limit;
    v->state.cr0=read_cr0();v->state.cr2=read_cr2();v->state.cr3=read_cr3();v->state.cr4=read_cr4();
    v->state.efer=rdmsr(MSR_EFER)|EFER_SVME;v->state.gpat=rdmsr(MSR_PAT);
    v->state.rflags=read_flags()|2;v->state.rsp=guest_rsp;v->state.rip=guest_rip;v->state.dr6=0xffff0ff0;v->state.dr7=0x400;
    v->state.rax=1; cpu->first_cr3=v->state.cr3;cpu->state=HV_CPU_BOOTSTRAP;
    cpuid(1,0,&a,&b,&c,&d);cpu->apic_id=current_apic_id();
    /*
     * VMLOAD consumes state that only VMSAVE populates (FS/GS/TR/LDTR and
     * syscall-related MSRs).  Seed those fields from the current UEFI context
     * before the assembly loop executes its first VMLOAD.
     */
    __asm__ __volatile__("vmsave %0"::"a"((uint64_t)(uintptr_t)v):"memory");
    /* Restore fields owned by our captured continuation after VMSAVE. */
    v->state.rsp=guest_rsp;v->state.rip=guest_rip;v->state.rax=1;
    v->state.cr0=read_cr0();v->state.cr2=read_cr2();v->state.cr3=read_cr3();v->state.cr4=read_cr4();
    v->state.rflags=read_flags()|2;v->state.efer=rdmsr(MSR_EFER)|EFER_SVME;v->state.gpat=rdmsr(MSR_PAT);
    if(bsp)file_log("BSP GUEST VMCB SAVED");
    wrmsr(SVM_MSR_HSAVE_PA,(uint64_t)(uintptr_t)cpu->hsave);
    __asm__ __volatile__("vmsave %0"::"a"((uint64_t)(uintptr_t)cpu->host_vmcb):"memory");
    __asm__ __volatile__("fxsave64 %0":"=m"(cpu->fx_state));
    __asm__ __volatile__("fxsave64 %0":"=m"(cpu->host_fx_state));
    if(bsp)file_log("BSP HOST STATE SAVED");
    ring_log(cpu,HV_EVT_CPU_PREPARE,guest_rip,guest_rsp,v->state.cr3,cpu->apic_id);
    ring_log(cpu,HV_EVT_HOST_CONTEXT,cpu->host_cr3,cpu->host_gdtr.base,cpu->host_idtr.base,cpu->host_tr);
    return cpu;
}


static void EFIAPI virtualize_ap(void *context)
{
    HV_ROOT *root = context;
    EFI_MP_SERVICES_PROTOCOL *mp = root->mp;
    UINTN processor_number = 0;

    if (EFI_ERROR(mp->WhoAmI(mp, &processor_number)))
    {
        return;
    }

    if (processor_number >= root->cpu_count)
    {
        return;
    }

    if (!svm_enter_context_resume(&root->cpus[processor_number]))
    {
        return;
    }

    enable_runtime_intercepts(&root->cpus[processor_number]);

    __atomic_fetch_add(
        &root->virtualized_count,
        1,
        __ATOMIC_SEQ_CST
    );

    ring_log(
        &root->cpus[processor_number],
        HV_EVT_CPU_GUEST,
        root->virtualized_count,
        root->enabled_cpu_count,
        0,
        0
    );
}

static int setup_cpus(void)
{
    EFI_MP_SERVICES_PROTOCOL *mp=NULL; EFI_STATUS s; UINTN i; uint8_t *stack;
    s=g_root.st->BootServices->LocateProtocol(&g_mp_guid,NULL,(void**)&mp);
    if(EFI_ERROR(s)||!mp){file_log_value("MP LOCATE FAILED=",s);return 0;}
    g_root.mp=mp;s=mp->GetNumberOfProcessors(mp,&g_root.cpu_count,&g_root.enabled_cpu_count);
    if(EFI_ERROR(s)||g_root.cpu_count==0||g_root.cpu_count>HV_MAX_CPUS){file_log_value("MP COUNT FAILED=",s);return 0;}
    s=mp->WhoAmI(mp,&g_root.bsp_number);
    if(EFI_ERROR(s)||g_root.bsp_number>=g_root.cpu_count){file_log_value("MP WHOAMI FAILED=",s);return 0;}
    if(!make_host_page_tables(&g_root.host_cr3)){file_log("HOST CR3 ALLOCATION FAILED");return 0;}
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,(sizeof(HV_CPU)*g_root.cpu_count+4095)/4096,(void**)&g_root.cpus))){file_log("CPU ARRAY ALLOCATION FAILED");return 0;}
    for(i=0;i<g_root.cpu_count;i++){
        HV_CPU *c=&g_root.cpus[i];c->processor_number=(uint32_t)i;
        if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&c->vmcb))){file_log_value("VMCB ALLOCATION CPU=",i);return 0;}
        if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,(void**)&c->host_vmcb))){file_log_value("HOST VMCB ALLOCATION CPU=",i);return 0;}
        if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,&c->hsave))){file_log_value("HSAVE ALLOCATION CPU=",i);return 0;}
        if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,HV_HOST_STACK_PAGES,(void**)&stack))){file_log_value("HOST STACK ALLOCATION CPU=",i);return 0;}
        c->host_stack_top=stack+HV_HOST_STACK_PAGES*4096;
        if(!prepare_host_context(c)){file_log_value("HOST CONTEXT PREPARE CPU=",i);return 0;}
    }
    return 1;
}


static int virtualize_aps(void)
{
    EFI_MP_SERVICES_PROTOCOL *mp;
    EFI_STATUS status;
    UINTN *failed = NULL;
    UINTN index;

    if (g_root.enabled_cpu_count <= 1)
    {
        return 1;
    }

    mp = g_root.mp;

    status = mp->StartupAllAPs(
        mp,
        virtualize_ap,
        TRUE,
        NULL,
        10000000,
        &g_root,
        &failed
    );

    if (EFI_ERROR(status))
    {
        file_log_value("StartupAllAPs status=", status);

        if (failed != NULL)
        {
            for (
                index = 0;
                failed[index] != HV_END_OF_CPU_LIST;
                index++
            )
            {
                file_log_value(
                    "AP failed processor=",
                    failed[index]
                );
            }

            g_root.st->BootServices->FreePool(failed);
        }

        return 0;
    }

    if (g_root.virtualized_count != g_root.enabled_cpu_count)
    {
        file_log_value(
            "VIRTUALIZED CPU COUNT=",
            g_root.virtualized_count
        );

        file_log_value(
            "EXPECTED CPU COUNT=",
            g_root.enabled_cpu_count
        );

        return 0;
    }

    return 1;
}

static int load_windows_boot_manager(void)
{
    EFI_HANDLE *handles=NULL; UINTN count=0,i; EFI_DEVICE_PATH *path; EFI_STATUS s;
    s=g_root.st->BootServices->LocateHandleBuffer(ByProtocol,&g_sfs_guid,NULL,&count,&handles);
    if(EFI_ERROR(s))return 0;
    for(i=0;i<count;i++){
        EFI_FILE_IO_INTERFACE *fs=NULL;EFI_FILE_HANDLE root=NULL,f=NULL;
        if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(handles[i],&g_sfs_guid,(void**)&fs))||!fs)continue;
        if(EFI_ERROR(fs->OpenVolume(fs,&root))||!root)continue;
        s=root->Open(root,&f,L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",EFI_FILE_MODE_READ,0);
        if(!EFI_ERROR(s)&&f){f->Close(f);root->Close(root);path=FileDevicePath(handles[i],L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
            if(path&&!EFI_ERROR(g_root.st->BootServices->LoadImage(FALSE,g_root.image,path,NULL,0,&g_root.bootmgfw))){FreePool(handles);return 1;}}
        else root->Close(root);
    }
    FreePool(handles);return 0;
}

static int svm_is_usable(void)
{
    uint32_t a,b,c,d,max;
    cpuid(0x80000000,0,&max,&b,&c,&d);if(max<0x8000000a)return 0;
    cpuid(0x80000001,0,&a,&b,&c,&d);if(!(c&(1U<<2))||!(d&(1U<<26)))return 0;
    cpuid(0x8000000a,0,&a,&b,&c,&d);if(!(d&1U))return 0; /* nested paging */
    if(rdmsr(SVM_MSR_VM_CR)&(1ULL<<4))return 0; /* SVMDIS */
    return 1;
}

static int log_runtime_image_type(void)
{
    EFI_LOADED_IMAGE *li=NULL;EFI_MEMORY_DESCRIPTOR *map=NULL,*d;
    UINTN size=0,key=0,ds=0,off;UINT32 version=0;EFI_STATUS s;uint64_t base;
    if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(g_root.image,&g_loaded_image_guid,(void**)&li))||!li)return 0;
    base=(uint64_t)(uintptr_t)li->ImageBase;
    file_log_value("RUNTIME IMAGE BASE=",base);
    s=g_root.st->BootServices->GetMemoryMap(&size,NULL,&key,&ds,&version);
    if(s!=EFI_BUFFER_TOO_SMALL||!ds)return 0;
    size+=ds*4;if(EFI_ERROR(g_root.st->BootServices->AllocatePool(EfiLoaderData,size,(void**)&map)))return 0;
    s=g_root.st->BootServices->GetMemoryMap(&size,map,&key,&ds,&version);
    if(EFI_ERROR(s)){g_root.st->BootServices->FreePool(map);return 0;}
    for(off=0;off<size;off+=ds){d=(EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map+off);
        if(base>=d->PhysicalStart&&base<d->PhysicalStart+d->NumberOfPages*4096){
            UINT32 type=d->Type;
            file_log_value("IMAGE MEMORY TYPE=",d->Type);
            file_log_value("IMAGE MEMORY ATTR=",d->Attribute);
            g_root.st->BootServices->FreePool(map);return type==EfiRuntimeServicesCode;
        }}
    g_root.st->BootServices->FreePool(map);return 0;
}

static int suppress_image_relocation(void)
{
    EFI_LOADED_IMAGE *li=NULL;uint8_t *base;uint32_t peoff,*signature;
    if(EFI_ERROR(g_root.st->BootServices->HandleProtocol(g_root.image,&g_loaded_image_guid,(void**)&li))||!li)return 0;
    base=(uint8_t*)li->ImageBase;
    if(li->ImageSize<0x40||*(uint16_t*)base!=0x5a4d)return 0;
    peoff=*(uint32_t*)(base+0x3c);
    if((uint64_t)peoff+4>li->ImageSize)return 0;
    signature=(uint32_t*)(base+peoff);
    if(*signature!=0x00004550U)return 0;
    *signature=0;return 1;
}


static void force_host_rendezvous(void)
{
    uint64_t value;

    value = rdmsr(SVM_MSR_VM_CR);
    (void)value;
}


static VOID EFIAPI on_exit_boot_services(
    EFI_EVENT event,
    VOID *context
)
{
    HV_ROOT *root = context;
    HV_CPU *bsp;

    (void)event;

    root->exit_boot_seen = 1;
    bsp = &root->cpus[root->bsp_number];

    if (!HV_VIRTUALIZE_ALL_CPUS)
    {
        root->sipi_remaining = 0;
        root->apic_started_count = 0;

        ring_log(
            bsp,
            HV_EVT_EXIT_BOOT,
            0,
            root->cpu_count,
            1,
            0
        );

        framebuffer_stage(
            HV_STAGE_NATIVE_APS,
            0x00008844U
        );

        bsp->vmcb->control.intercept_misc1 |= SVM_INTERCEPT_CPUID;
        bsp->vmcb->control.vmcb_clean = 0;

        force_host_rendezvous();
        return;
    }

    root->sipi_remaining = (uint32_t)(
        (root->enabled_cpu_count > 1)
            ? (root->enabled_cpu_count - 1) * 2
            : 0
    );

    root->apic_started_count = 0;

    ring_log(
        bsp,
        HV_EVT_EXIT_BOOT,
        root->sipi_remaining,
        root->cpu_count,
        0,
        0
    );

    framebuffer_stage(
        HV_STAGE_EBS,
        0x00555500U
    );

    if (
        !root->x2apic &&
        root->sipi_remaining != 0 &&
        !root->xapic_intercept_active
    )
    {
        xapic_set_intercept(1);

        framebuffer_stage(
            HV_STAGE_APIC_ON,
            0x000000ccU
        );
    }

    /*
     * Apply the NPT permission change before Windows sends INIT/SIPI.
     */
    force_host_rendezvous();
}


static VOID EFIAPI on_virtual_address_change(
    EFI_EVENT event,
    VOID *context
)
{
    HV_ROOT *root = context;

    (void)event;

    root->virtual_mode_seen = 1;

    ring_log(
        &root->cpus[root->bsp_number],
        HV_EVT_VIRTUAL_MODE,
        root->sipi_remaining,
        root->cpu_count,
        0,
        0
    );

    if (
        HV_VIRTUALIZE_ALL_CPUS &&
        !root->x2apic &&
        root->sipi_remaining != 0 &&
        !root->xapic_intercept_active
    )
    {
        root->apic_activation_pending = 1;
    }
}

EFI_STATUS hv_runtime_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st)
{
    EFI_STATUS s; UINTN exit_size=0; CHAR16 *exit_data=NULL; uint64_t apic;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;
    InitializeLib(image,st);memzero(&g_root,sizeof(g_root));g_root.st=st;g_root.image=image;
    st->BootServices->SetWatchdogTimer(0,0,0,NULL);
    if(!EFI_ERROR(st->BootServices->LocateProtocol(&GraphicsOutputProtocol,NULL,(void**)&gop))&&gop&&gop->Mode&&gop->Mode->Info&&
       (gop->Mode->Info->PixelFormat==PixelRedGreenBlueReserved8BitPerColor||
        gop->Mode->Info->PixelFormat==PixelBlueGreenRedReserved8BitPerColor)){
        g_root.framebuffer_base=gop->Mode->FrameBufferBase;
        g_root.framebuffer_width=gop->Mode->Info->HorizontalResolution;
        g_root.framebuffer_height=gop->Mode->Info->VerticalResolution;
        g_root.framebuffer_stride=gop->Mode->Info->PixelsPerScanLine;
        g_root.framebuffer_bgr=(gop->Mode->Info->PixelFormat==PixelBlueGreenRedReserved8BitPerColor);
    }
    Print(L"=== Transparent AMD-SVM context-resume loader ===\nBuild: %a\n",HV_BUILD_TAG);file_log("=== BOOTX64.EFI entered ===");file_log("HV build tag: " HV_BUILD_TAG);
    if(!log_runtime_image_type()){
        file_log("IMAGE IS NOT RUNTIME SERVICES CODE");
        Print(L"EFI image was not loaded as runtime code.\n");return EFI_UNSUPPORTED;
    }
    file_log("RUNTIME IMAGE RESIDENCY CONFIRMED");
    export_previous_ring();
    if(!svm_is_usable()){Print(L"SVM/NPT/1GB pages unavailable or SVM locked.\n");file_log("SVM prerequisites failed");return EFI_UNSUPPORTED;}
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,(sizeof(HV_LOG_RING)+4095)/4096,(void**)&g_root.log)))return EFI_OUT_OF_RESOURCES;
    g_root.log->magic=HV_LOG_MAGIC;g_root.log->version=HV_LOG_VERSION;
    {uint64_t address=(uint64_t)(uintptr_t)g_root.log;st->RuntimeServices->SetVariable(
        L"HvTransparentSvmLogAddress",&g_log_guid,EFI_VARIABLE_NON_VOLATILE|EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(address),&address);}
    apic=rdmsr(MSR_APIC_BASE);g_root.x2apic=(apic&(1ULL<<10))!=0;
    Print(L"APIC mode: %s\n",g_root.x2apic?L"x2APIC":L"xAPIC");
    file_log(g_root.x2apic?"APIC MODE X2APIC":"APIC MODE XAPIC");
    file_log("NPT BUILD BEGIN");
    if(!make_npt(0,&g_root.npt_root,NULL))return EFI_OUT_OF_RESOURCES;
    if(!g_root.x2apic){
        if(!make_npt(0,&g_root.npt_bsp_root,&g_apic_npt_pte))return EFI_OUT_OF_RESOURCES;
        if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,1,&g_root.apic_shadow_page)))return EFI_OUT_OF_RESOURCES;
    }else g_root.npt_bsp_root=g_root.npt_root;
    file_log("NPT BUILD COMPLETE");
    if(EFI_ERROR(alloc_pages(EfiACPIMemoryNVS,2,&g_root.msrpm)))return EFI_OUT_OF_RESOURCES;
    /*
     * Trap EFER so Windows can use SCE/LME/LMA/NXE normally while SVME stays
     * hidden. Nested SVM is not implemented by this hypervisor.
     */
    msrpm_set(MSR_EFER,1,1);
    msrpm_set(SVM_MSR_VM_CR,1,1);
    msrpm_set(SVM_MSR_HSAVE_PA,1,1);
    msrpm_set(MSR_X2APIC_ICR,0,1);
    file_log("WINDOWS LOADIMAGE BEGIN");
    if(!load_windows_boot_manager()){Print(L"Windows bootmgfw.efi not found.\n");file_log("Windows bootmgfw.efi not found");return EFI_NOT_FOUND;}
    file_log("WINDOWS LOADIMAGE COMPLETE");file_log("MP CPU SETUP BEGIN");
    if(!setup_cpus()){Print(L"MP/SVM CPU setup failed.\n");file_log("MP/SVM CPU setup failed");return EFI_UNSUPPORTED;}
    file_log_value("MP CPU COUNT=",g_root.cpu_count);file_log_value("MP ENABLED COUNT=",g_root.enabled_cpu_count);
    file_log("BSP VMRUN BEGIN");
    /* BSP returns from this call only after VMRUN resumed the same context. */
    if(!svm_enter_context_resume(&g_root.cpus[g_root.bsp_number]))return EFI_ABORTED;
    /* Reaching here is the architectural proof that the BSP resumed in guest mode. */
    enable_runtime_intercepts(&g_root.cpus[g_root.bsp_number]);
    __atomic_fetch_add(&g_root.virtualized_count,1,__ATOMIC_SEQ_CST);g_root.log->cpu_count=(uint32_t)g_root.cpu_count;
    ring_log(&g_root.cpus[g_root.bsp_number],HV_EVT_CPU_GUEST,g_root.virtualized_count,g_root.cpu_count,0,0);
    file_log("BSP RESUMED IN GUEST");
    if (!HV_VIRTUALIZE_ALL_CPUS)
    {
        file_log("SAFE BOOT: AP VIRTUALIZATION SKIPPED");

        Print(
            L"SAFE BOOT: BSP virtualized, APs left native: %u/%u\n",
            g_root.virtualized_count,
            g_root.cpu_count
        );
    }
    else
    {
        file_log("AP VIRTUALIZATION BEGIN");

        if (!virtualize_aps())
        {
            Print(L"AP virtualization failed.\n");
            file_log("AP VIRTUALIZATION FAILED");
            return EFI_TIMEOUT;
        }

        file_log("AP PROCESSORS VIRTUALIZED");

        Print(
            L"ALL PROCESSORS VIRTUALIZED: %u/%u\n",
            g_root.virtualized_count,
            g_root.enabled_cpu_count
        );

        file_log("ALL PROCESSORS VIRTUALIZED");
    }
    s=st->BootServices->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES,TPL_NOTIFY,
        on_exit_boot_services,&g_root,&g_root.exit_boot_event);
    if(EFI_ERROR(s)){file_log_value("EXIT BOOT EVENT CREATE FAILED=",s);return s;}
    file_log("EXIT BOOT SERVICES HOOK REGISTERED");
    /*
     * Do not execute a callback from HVCORE.EFI during SetVirtualAddressMap.
     * The host context is now installed by the forced post-EBS VMMCALL.
     */
    g_root.virtual_address_event=NULL;
    file_log("VIRTUAL ADDRESS CHANGE HOOK DISABLED");
    if(!suppress_image_relocation()){
        file_log("RUNTIME IMAGE RELOCATION SUPPRESSION FAILED");return EFI_LOAD_ERROR;
    }
    g_root.boot_started=1;file_log("STARTIMAGE CALLED FROM GUEST");
    s=st->BootServices->StartImage(g_root.bootmgfw,&exit_size,&exit_data);
    Print(L"Windows StartImage returned: 0x%lx\n",s);file_log("Windows StartImage returned unexpectedly");
    log_hex_line(L"EFI status",s);return s;
}

EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st)
{
    EFI_LOADED_IMAGE *loaded=NULL;EFI_DEVICE_PATH *path=NULL;EFI_HANDLE core=NULL;EFI_STATUS s;
    UINTN exit_size=0;CHAR16 *exit_data=NULL;
    InitializeLib(image,st);
    s=st->BootServices->HandleProtocol(image,&g_loaded_image_guid,(void**)&loaded);
    if(EFI_ERROR(s)||!loaded){Print(L"Cannot locate bootstrap loaded image: 0x%lx\n",s);return s;}
    path=FileDevicePath(loaded->DeviceHandle,L"\\EFI\\BOOT\\HVCORE.EFI");
    if(!path)return EFI_OUT_OF_RESOURCES;
    s=st->BootServices->LoadImage(FALSE,image,path,NULL,0,&core);
    FreePool(path);
    if(EFI_ERROR(s)){Print(L"Cannot load HVCORE.EFI: 0x%lx\n",s);return s;}
    s=st->BootServices->StartImage(core,&exit_size,&exit_data);
    Print(L"HVCORE.EFI returned unexpectedly: 0x%lx\n",s);
    return s;
}
