// The ELF entry point has an unmangled name and never returns.
extern "C" [[noreturn]] void KernelMain()
{
    for (;;) {
        asm volatile("cli; hlt");
    }
}
