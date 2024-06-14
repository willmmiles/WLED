
    .section .iram.text
    .align    4
    .literal_position

int_vectors_ptr: .int int_vectors    
isr_idx_ptr: .int event_slot_index
rtc_mem_ptr: .int 0x60001200

    .global   track_isr_hook
    .type     track_isr_hook, @function
track_isr_hook:
    // A3 is available, A4 has been spilled to the stack
    // Load isr_idx to a4 and increment it; use preincrement to minimize register use
    l32r a3, isr_idx_ptr
    l32i a4, a3, 0x0
    addi.n a4, a4, 0x01
    s32i.n a4, a3, 0x0
    movi a3, 0x0F
    and a4, a4, a3
    movi a3, 0x18 // sizeof(isr_data)
    mull a4, a4, a3
    l32r a3, rtc_mem_ptr
    add a4, a4, a3

    // store exccause
    s32i a2, a4, 0
    addi.n a4, a4, 4

    rsr a3,epc1
    s32i a3, a4, 0
    addi.n a4, a4, 4

    s32i a1, a4, 0
    addi.n a4, a4, 4

    rsr a3,ccount
    s32i a3, a4, 0
    addi.n a4, a4, 4

    rsr a3, interrupt
    rsr a0, intenable
    slli a0, a0, 16
    add.n a3, a3, a0
    rsr a0, excsave1    // restore a0 (??)
    s32i a3, a4, 0
    addi.n a4, a4, 4
    
    xor a3, a3, a3
    s32i a3, a4, 0

    // now load the old vector
    l32r a3, int_vectors_ptr
    addx4 a3,a2,a3
    l32i.n a3,a3,0x0
    jx a3
    .size    track_isr_hook, . - track_isr_hook
