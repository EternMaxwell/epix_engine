# Documentation TODO — Cross-module tracker

This page tracks project-wide directions. Concrete implementation gaps live beside
their modules so that each item can be checked against the current code:

- [ECS](ecs/todo.md)
- [Task](task/todo.md)
- [Assets](assets/todo.md)
- [Time](time/todo.md)
- [Input](input/todo.md)
- [Window](window/todo.md)
- [Shader](shader/todo.md)
- [Render](render/render/todo.md)

## Language and standard-library direction

The engine currently targets C++23. Future migrations may use standardized static
reflection and execution facilities once the required compiler and standard-library
support is practical across the supported toolchains.

Static reflection could reduce the boilerplate around ECS traits such as `Bundle`
and component metadata. Standard execution facilities could eventually replace
parts of the task module's Asio-specific implementation and make coroutine
interoperability easier. These are directions rather than committed release
requirements; public APIs should continue to describe what the repository supports
today.

Longer-term language proposals such as compile-time code injection, interface-class
syntax, pattern matching, and uniform function-call syntax may also affect API
design. They should not be treated as current engine features until they are
standardized and supported by the project's toolchains.
