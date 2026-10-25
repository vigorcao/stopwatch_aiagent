// Browser entry point: runs the unmodified main/main.cpp app_main().
// ASYNCIFY lets its infinite loop yield to the browser via Hal::feedTheDog().
extern "C" void app_main(void);

int main()
{
    app_main();
    return 0;
}
