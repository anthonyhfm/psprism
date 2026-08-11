#include <pspkernel.h>
#include <pspsysmem.h>

PSP_MODULE_INFO("relocatable_fixture", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static volatile int fixture_value = 7;
static volatile int *fixture_pointer = &fixture_value;

__attribute__((noinline)) float unchanged_callee(float value) {
  return value * 3.0F;
}

__attribute__((noinline)) int overlay_target(int value) {
  __asm__ volatile("vmov.s S000, S000");
  return (int)unchanged_callee((float)value) + 7;
}

int main(int argument_count, char **arguments) {
  (void)arguments;
  const int result = overlay_target(*fixture_pointer + argument_count);
  sceKernelPrintf("PSPRECOMP_OVERLAY_RESULT=%d\n", result);
  sceKernelExitGame();
  return result + sceKernelGetThreadId();
}
