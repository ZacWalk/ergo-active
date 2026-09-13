#include <string_view>

int main()
{
    constexpr std::string_view name = "ergo-active";
    return name.empty() ? 1 : 0;
}
