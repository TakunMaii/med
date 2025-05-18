# MedScript

## Overview
MedScript is a simple programming language designed for configuring med the text editor.

Med itself is also an interpreter for MedScript.

## Syntax
The syntax of MedScript is sort of like lua but with some difference. For example, if you want to create a variable, you can do it like this:
```
a = 1
```
There is no need to declare the type of the variable, and you can assign a value to it directly. The type of the variable will be determined by the value you assign to it.

You can use common `if`, `else`, `while` statements:
```
if (a==1)
    print("a is 1")
else if (a==2)
    print("a is 2")
else
    print("a is not 1 or 2")
end
```
You may notice that MedScript does not use `{}` to enclose the code block. Instead, it uses `end` to mark the end of the code block, which is same when using `while`:
```
while (a<10)
    a = a + 1
end
```
It is worth mentioning that you do not need to obey strict rule of indentation (like what python does), which means you can write the code like this:
```
if (a==1) a = 1 else a = 2 end
```
or this:
```
while (a<10) a = a + 1 end
```
When you want to do more than one thing in a single line, just seperate them with `,`:
```
a = 1, b = 2, c = 3
```
Therefore you can also do this:
```
while (a<10) a = a + 1, b = b + 1 end
```
You can make comments like this:
```
# this is a comment
```
## Flow Control
The code will be executed line by line, from top to bottom.

You can use `if`, `else`, `while` (*NO `for`*) to control the flow of the code like what has been mentioned above.

You can also use `break` to break out of a loop:
```
while (a<10)
    if (a==5)
        break
    end
    a = a + 1
end
```
You can use `continue` to skip the current iteration of a loop:
```
while (a<10)
    if (a==5)
        continue
    end
    a = a + 1
end
```
## Variables
There are only 5 types of variables in MedScript:
+ integer
+ float
+ string
+ table
+ function

The first three types are quite common and simple, you can declare them like this:
```
a = 1
b = 1.0
c = "hello"
```
### Tables
A table is a collection of key-value pairs, where keys are strings and values can be any type. You can create a table like this:
```
a = {
    name = "hello",
    age = 18,
    height = 1.8,
    weight = 60,
}
```
or like this:
```
a = {
    "hello",
    18,
    1.8,
    60,
}
```
In this case, the keys are the index of the value, **starting from 0**.

You can access the value in the table like this:
```
a.name = "hello"
```
The following ways are also valid:
```
a[0] = "hello"
a.0 = "hello"
a[name] = "hello"
```
### Functions
A function is a block of code that can be called by its name. You can create a function like this:
```
function add(a, b)
    return a + b
end
```
You can also create a function in a variable-like way:
```
add = function(a, b)
    return a + b
end
```
They are completely equivalent.

You can call a function like this:
```
result = add(1, 2)
```
## Acessibility of Variables
There are two types of variables in MedScript: global and local. A global variable is a variable that can be accessed from anywhere in the code, while a local variable is a variable that can only be accessed from within the function where it is defined.

Specifically, there is a table containing all the global variables and whenever you call a function, the interpreter will create a new table for the local variables, which will be recycled when the function returns. When a function is called within another one, there will be one more table created for the local variables.

Every time when you reference a variable, the interpreter will first check if it is a local variable within the current scope. If it is not, the interpreter will check if it is a variable in the variable table of the very upper function calling stack, layer by layer. Finally it will check if it is a global variable.

The following is an example:
```
a = 1
function dosth()
    function dosth2()
        a = 3
        print(a)
    end
    dosth2()
    print(a)
end
dosth()
```
The output of the above code will be:
```
3
1
```

## Garbage Collection (?)
There is *NO* garbage collection in MedScript, which means you need to manage the memory yourself. You can use `recycle` to delete a variable:
```
a = 2
recycle a
```
