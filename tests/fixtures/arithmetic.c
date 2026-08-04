#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
__asm__(".set noreorder\n"
        ".section .text.recomp_post_delay_entry, \"ax\", @progbits\n"
        ".globl recomp_post_delay_entry\n"
        "recomp_post_delay_entry:\n"
        "b 2f\n"
        "nop\n"
        "1:\n"
        "jr $ra\n"
        "li $v0, 42\n"
        "2:\n"
        "jr $ra\n"
        "move $v0, $zero\n"
        ".previous\n"
        ".section .text.recomp_seb, \"ax\", @progbits\n"
        ".globl recomp_seb\n"
        "recomp_seb:\n"
        ".word 0x7c048c20\n"
        ".word 0x7c051620\n"
        ".word 0x66828203\n"
        ".word 0x64222368\n"
        ".word 0x7ca6b004\n"
        ".word 0xf13f9837\n"
        ".word 0xd0392908\n"
        ".word 0xd0380404\n"
        ".word 0xd29f8404\n"
        ".word 0xd0160300\n"
        ".word 0x6f959496\n"
        ".word 0xd0419697\n"
        ".word 0xd0499594\n"
        ".word 0xd0154300\n"
        ".word 0xd0140023\n"
        ".word 0xf38680a0\n"
        ".word 0xf38380b0\n"
        ".word 0xf4800002\n"
        ".word 0xd03a0006\n"
        ".word 0xd03e0284\n"
        ".word 0xf2638484\n"
        ".word 0xd0468672\n"
        ".word 0xd04a8002\n"
        ".word 0x6e999813\n"
        ".word 0x00604816\n"
        ".word 0xd0650020\n"
        ".word 0xd0058282\n"
        ".word 0xf380a421\n"
        ".word 0x04d80303\n"
        ".word 0x00000001\n"
        "jr $ra\n"
        "move $v0, $s1\n"
        ".previous\n"
        ".set reorder\n");
#endif

__attribute__((noinline)) unsigned recomp_test(unsigned a, unsigned b) {
    unsigned result = a ^ 0x13579bdfU;
    for (unsigned i = 0; i < 7; ++i) {
        result = (result << 5) | (result >> 27);
        result += b ^ (i * 0x10203U);
    }
    return result;
}

#ifdef __cplusplus
}
#endif
