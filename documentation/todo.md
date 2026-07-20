# Documentation TODO — Cross-module tracker

Currently under refactor, so no todo for new codes yet.

But we added:

## New plan and future updates, mostly targeting cpp26 and cpp29

This part will be always reserve until we actually migrate to use them.

## CPP 26

Cpp 26 introduced static reflection and execution library, these two will be prioritized whenevery three compilers supports them.

### static reflection

With reflection, we will introduce more human-friendly trait implement api, for things like `Bundle`, `Componeent` or and many other trait structs. And we will change to interface struct from virtual function for stuffs like `Plugin`, or maybe delay this change to cpp29 with `queue_injection` and `class(interface)`.

### execution

With execution library, we no longer need to be stuck on asio structs/helpers and its runtime, and can have easier written coroutine code and better async api, targetting std::execution. We will have more compatibility with other coroutine libraries or libraries utilize coroutine.

## CPP 29

We expect `queue_injection` and `class(interface)` to be adopted in cpp29, it would be better if **pattern matching** and **UFCS**(uniform function call, to make free function able to be called with member function calling syntax)

### queue injection

This is the code generating part, with this, we can easily achieve code generation at compile time, to produce better api, and reduce redundant work, on writting same code every time.

Might also be used for **changing to interface structs** if static reflection and `class(interface)` cannot achieve this or not adopted.

### class(interface)

Just a grammar sugar for fast writing code. But will dramatically improve our codes.

### pattern matching

Can be seen as another grammar sugar, and make us write better/more straight-forward code.

### UFCS

If this is accepted, then it changes everything. This will impact how we organize our code.

Previously, if we want to add function to a type, to make it possible to be called as `instance.func()`, we have to make the function member function, but what if the types used by the function relies on this type, then we have to forward declare those types and define it later. If it is not templated, we can easily put it in .cpp file, but if template, it is hard to deside where to put it as well. And what if we want to add function to it in other modules, and still can be called with member function call syntax - just no way currently.

But with **UFCS**, we can directly achieve things similar to `impl Trait` or `impl Type` in `rust`. This can reduce a lot of work we previously need to do and a lot of concerns we previously have.
