#include <ddstd/optional.h>
#include <ddstd/string_view.h>
#include <ddstd/variant.h>

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    ddstd::variant<int, std::string> value;
    if (argc % 2 == 0) {
        value = argc;
    } else {
        value = "odd number of arguments";
    }

    std::cout << value << '\n';
    
    ddstd::optional<std::string> maybe_words;
    if (argc > 1) {
        maybe_words = "";
        for (auto word = argv + 1; *word; ++word) {
            maybe_words.value() += ' ';
            maybe_words.value() += *word;
        } 
    }
    std::cout << "maybe_words: " << maybe_words.value_or("empty") << '\n';
    
    std::cout << ddstd::string_view("hello").size() << '\n';
}
