; ModuleID = 'practice1'
source_filename = "practice1"
target triple = "aarch64-unknown-linux-gnu"

@0 = private unnamed_addr constant [29 x i8] c"Program exit with result %d\0A\00", align 1

define i32 @main() {
entry:
  %f = alloca i32, align 4
  store i32 67, ptr %f, align 4
  %a = alloca i32, align 4
  store i32 6, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 7, ptr %b, align 4
  %a1 = load i32, ptr %a, align 4
  %mul = mul i32 10, %a1
  store i32 %mul, ptr %b, align 4
  %f2 = load i32, ptr %f, align 4
  %b3 = load i32, ptr %b, align 4
  %sub = sub i32 %f2, %b3
  store i32 %sub, ptr %b, align 4
  %b4 = load i32, ptr %b, align 4
  %0 = call i32 (ptr, ...) @printf(ptr @0, i32 %b4)
  ret i32 0
}

declare i32 @printf(ptr, ...)
