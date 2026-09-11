; ModuleID = 'practice1'
source_filename = "practice1"
target triple = "aarch64-unknown-linux-gnu"

@0 = private unnamed_addr constant [29 x i8] c"Program exit with result %d\0A\00", align 1

define i32 @main() {
entry:
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  %c = alloca i32, align 4
  store i32 7, ptr %a, align 4
  %a.val = load i32, ptr %a, align 4
  %mul = mul i32 %a.val, 3
  store i32 %mul, ptr %b, align 4
  %b.val = load i32, ptr %b, align 4
  %sub = sub i32 %b.val, 1
  store i32 %sub, ptr %c, align 4
  %c.val = load i32, ptr %c, align 4
  %0 = call i32 (ptr, ...) @printf(ptr @0, i32 %c.val)
  ret i32 0
}

declare i32 @printf(ptr, ...)
