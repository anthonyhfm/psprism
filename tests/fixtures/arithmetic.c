#ifdef __cplusplus
extern "C" {
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

