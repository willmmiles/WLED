// ISR event tracking hooks


// Global variables
.section .noinit
.align 4
// Assume we won't see nested exceptions, except for NMI
reg_spill: .skip 64
reg_spill_nmi: .skip 64
last_epx: .skip 8



.section .iram.text
.literal_position

// Instrumented exception vector table
.align 256
.global InstrumentedVectorTable
.type   InstrumentedVectorTable, @function
InstrumentedVectorTable:
    // Startup code (??)
    .space 16, 0
InstrumentedDebugExceptionVector:
    wsr a0, excsave1
    j track_debugexception
    .space 16 - (. - InstrumentedDebugExceptionVector), 0
InstrumentedNMIExceptionVector:
    wsr a0, excsave3
    j track_nmi
    //call0 _NMILevelVector
    .space 16 - (. - InstrumentedNMIExceptionVector), 0
InstrumentedKernelExceptionVector:
    wsr a0, excsave1
    j track_kernelexception
    .space 32 - (. - InstrumentedKernelExceptionVector), 0  //? why 32?
InstrumentedUserExceptionVector:
    wsr a0, excsave1
    j track_userexception
    //call0 _UserExceptionVector_1
    .space 32 - (. - InstrumentedUserExceptionVector), 0
InstrumentedDoubleExceptionVector:
    wsr a0, excsave1
    j track_doubleexception
    .space 32 - (. - InstrumentedDoubleExceptionVector), 0

// Common hook macros
.macro spill_regs
    s32i.n a1,a0,0x04
    s32i.n a2,a0,0x08
    s32i.n a3,a0,0x0C
    s32i.n a4,a0,0x10
.endm

.macro restore_regs
    l32i.n a4,a0,0x10
    l32i.n a3,a0,0x0C
    l32i.n a2,a0,0x08
    l32i.n a1,a0,0x04
.endm

.macro track_exception id, spill_ptr=reg_spill_ptr
    l32r a0, \spill_ptr
    spill_regs
    movi.n a2, \id
    call0 track_isr_hook
    l32r a0, \spill_ptr
    restore_regs
.endm

// -----------
// Exception handlers
// -----------
.align    4

// data pointers
reg_spill_ptr: .int reg_spill
reg_spill_nmi_ptr: .int reg_spill_nmi
last_epx_ptr: .int last_epx
_NMILevelVector_ptr: .int _NMILevelVector
//_NMILevelVector_size: .int 0xBB // size of above

.align 4
.global   track_debugexception
.type     track_debugexception, @function
track_debugexception:
    track_exception 0x01
    rsr a0, excsave1
// The actual debugexception vector
InstrumentedDebugExceptionVector_loop:
    waiti 0x02
    j InstrumentedDebugExceptionVector_loop


.align 4
.global   track_nmi
.type     track_nmi, @function
track_nmi:
    //track_exception 0x02, reg_spill_nmi_ptr
    // expanded for additional check
    l32r a0, reg_spill_nmi_ptr
    spill_regs
    movi.n a2, 0x02
    call0 track_isr_hook
    rsr a0, epc3
    l32r a1, _NMILevelVector_ptr
    blt a0, a1, do_nmi  // all good
    addi.n a1, a1, 0xbb // length of nmi handler
    l32r a2, last_epx_ptr
    blt a0, a1, handle_crash   // WTF?!?
    s32i.n a0, a2, 0        // save last good epc3
    rsr a0, eps3
    s32i.n a0, a2, 4        // save eps3
do_nmi:        
    l32r a0, reg_spill_nmi_ptr
    restore_regs
    
    rsr a0, excsave3
// The actual nmi vector.  Not sure what it does with the return address in a0??
    call0 _NMILevelVector

handle_crash:
    // attempt to recover.  assume no other regs have changed and just rewrite the epc reg with the last known good value.
    l32i.n a0, a2, 0
    wsr a0, epc3
    l32i.n a0, a2, 4
    wsr a0, eps3
    j do_nmi
/*
    movi.n a2, 0
    call0 NmiTimSetFunc
crashloop:
    j crashloop
*/

.align 4
.global   track_kernelexception
.type     track_kernelexception, @function
track_kernelexception:
    track_exception 0x03
    rsr a0, excsave1
// The actual kernelexception vector
InstrumentedKernelExceptionVector_loop:    
    break 0x1, 0x0
    j InstrumentedKernelExceptionVector_loop


.align 4
.global   track_userexception
.type     track_userexception, @function
track_userexception:
    track_exception 0x04
    rsr a0, excsave1
    call0 _UserExceptionVector_1


.align 4
.global   track_doubleexception
.type     track_doubleexception, @function
track_doubleexception:
    track_exception 0x05
    rsr a0, excsave1
InstrumentedDoubleExceptionVector_loop:        
    break 0x1, 0x4
    j InstrumentedDoubleExceptionVector_loop




// -----------
// Data pointers in IRAM
// -----------
.align 4
isr_idx_ptr: .int event_slot_index
event_mem_ptr: .int event_buf

// The actual interrupt tracking code
.align 4
.global   track_isr_hook
.type     track_isr_hook, @function
track_isr_hook:
    // Up to a4 is available; a2 holds code
    // Load isr_idx to a4 and increment it; use preincrement to minimize register use
    l32r a3, isr_idx_ptr
    l32i a4, a3, 0x0
    addi.n a4, a4, 0x01
    s32i.n a4, a3, 0x0
    movi a3, 0x7F
    and a4, a4, a3
    movi a3, 0x18 // sizeof(isr_data)
    mull a4, a4, a3
    l32r a3, event_mem_ptr
    add a4, a4, a3

    // store epc based on lvl.
    // we do this first because the lvl storage blows away the reg
    beqi a2, 0x02, track_epc3
    rsr a3, epc1
    j track_epc
track_epc3:
    rsr a3, epc3
track_epc:    
    s32i a3, a4, 4

    // store argument in lvl 0x80000000 + (100*arg) + exccause
    movi.n a3, 100
    mull a2, a2, a3
    movi.n a3, 0x80
    slli a3, a3, 24
    add.n a2, a2, a3
    rsr a3, exccause
    add.n a2, a2, a3
    s32i a2, a4, 0

    // stack pointer is in a1
    s32i a1, a4, 8

    // ccount
    rsr a3,ccount
    s32i a3, a4, 12

    // interrupt and intenable fit in 16 bits, so pack them
    rsr a3, interrupt
    rsr a2, intenable
    slli a2, a2, 16
    add.n a3, a3, a2    
    s32i a3, a4, 16

    // data field
    rsr a3, excsave3
    s32i a3, a4, 20

    ret.n
    .size    track_isr_hook, . - track_isr_hook
