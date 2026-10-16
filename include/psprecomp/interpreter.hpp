#pragma once

namespace psprecomp {
class Runtime;
struct AllegrexContext;

// One architectural instruction, including an explicit branch/delay-slot
// continuation in AllegrexContext. Used only when the runtime opts in.
void interpret_instruction(Runtime &runtime, AllegrexContext &context);
} // namespace psprecomp
