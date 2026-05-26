#include <exception>
#include <iostream>

int animus_app_main(int argc, char **argv);

int main(int argc, char **argv)
{
    try
    {
        return animus_app_main(argc, argv);
    }
    catch (const std::exception &error)
    {
        std::cerr << "animus: " << error.what() << '\n';
        return 1;
    }
}
