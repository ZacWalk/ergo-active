#include <string_view>
#include <cstdio>

int main()
{
	constexpr std::string_view name = "ergo-active";
	std::puts("ergo-active: build-only smoke; behavioural counts come from usage and dashboard suites");
	return name.empty() ? 1 : 0;
}
