/* Compiled with Android's TPIDR_EL0 + 40 stack-protector convention. */
int tls_guarded(int seed) {
    volatile unsigned char buffer[32];
    buffer[0] = seed;
    buffer[31] = seed + 1;
    return buffer[0] + buffer[31];
}
