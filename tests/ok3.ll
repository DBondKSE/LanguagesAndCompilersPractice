; ModuleID = 'practice1'
source_filename = "practice1"
target triple = "arm64-apple-darwin25.6.0"

@0 = private unnamed_addr constant [29 x i8] c"Program exit with result %d\0A\00", align 1

define i32 @main() {
entry:
  %x = alloca i32, align 4
  %y = alloca i32, align 4
  %t_1 = alloca i32, align 4
  store i32 10, ptr %x, align 4
  %x.val = load i32, ptr %x, align 4
  %sub = sub i32 %x.val, 4
  store i32 %sub, ptr %x, align 4
  %x.val1 = load i32, ptr %x, align 4
  store i32 %x.val1, ptr %y, align 4
  store i32 6, ptr %t_1, align 4
  %y.val = load i32, ptr %y, align 4
  %t_1.val = load i32, ptr %t_1, align 4
  %add = add i32 %y.val, %t_1.val
  store i32 %add, ptr %y, align 4
  %y.val2 = load i32, ptr %y, align 4
  %0 = call i32 (ptr, ...) @printf(ptr @0, i32 %y.val2)
  ret i32 0
}

declare i32 @printf(ptr, ...)
