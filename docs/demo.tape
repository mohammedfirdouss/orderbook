# Terminal demo for the README. Render from the repo root with: vhs docs/demo.tape
# Needs a Release build in build/ (cmake -S . -B build && cmake --build build).
Output docs/img/demo.gif

Set Shell "bash"
Set FontSize 15
Set Width 1200
Set Height 760
Set Padding 20
Set Theme "Catppuccin Mocha"
Set TypingSpeed 35ms

Hide
Type "export PS1='$ ' && clear"
Enter
Show

Type "# orderbook: price-time priority matching engine in C++20"
Enter
Sleep 800ms

Type "./build/tests | tail -6"
Enter
Sleep 3s

Type "# both ladders replay the same 5M orders"
Enter
Type "./build/bench"
Enter
Sleep 5s

Type "# wider book: orders spread over ~6,000 price levels"
Enter
Type "./build/bench 5000000 10000 2048 | grep -E 'resting|Ladder|throughput'"
Enter
Sleep 5s
