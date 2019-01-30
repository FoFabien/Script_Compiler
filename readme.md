# Script Compiler  
  
C++11 Class to compile and run simple user scripts.  
Currently in a Beta state.  
  
### Compile Time  
Four steps are taken:  
* Loading the file and breaking down the text in tokens. I used a regex previously but it's way too slow (removing the regex reduced the compile time by 75%).  
* Tokens are then passed to a shunting yard algorithm. It's the result of my previous [try in Python](https://github.com/FoFabien/Compiler-wip-) improved with more trial&error and discussions I read [online](https://stackoverflow.com/questions/16380234/handling-extra-operators-in-shunting-yard). The state machine is divided in 4 parts. GOTO allergics, turn back now.  
* The output is now sorted in a [Reverse Polish Notation (RPN)](https://en.wikipedia.org/wiki/Reverse_Polish_notation) and broken down further into simpler instructions (one operator/function with optional parameters and an optional variable for the return value).  
* Finally, some error checks and optimizations.  
  
### Run Time  
The script has a few different states: Stopped (default one), Paused, Running and Error.  
It's a simple loop, checking for the operator at the current line:  
* "Global functions" trigger a callback to the hard-coded function ("if", "else", "print", "return", etc... are such functions).  
* Math operators trigger a call to Script::operation.  
* "Local functions" cause the current state (variables, pc, etc...) to be put into a stack. New blank variables are created for the function call.  
  
### Script Language  
Random notes:  
* It's loosely based on the C/C++ syntax.  
* Variables are dynamically typed. The Value class is used to store a value/variable id. It supports currently integer, float and string types.
* Script::addGlobalFunction() can be used to add more hard-coded function. This must be used before both compiling and loading a script or the compiler won't be aware the function exists.  
* In the same way, Script::initGlobalVariables() can be used to create a specific number of "global variables" shared between all scripts. Then, to use the variable, type @ followed by the variable id (example: @0 for the first global variable, @1 for the second, etc...). Script::clearGlobalVariables() must be called at the end to clear the memory.  
* Local variables are only accessible in their current scope. A variable V in the function foo() won't be the same as a variable V in the main/default scope or any other function. Same thing if you have a recursive function bar(), different calls have a different "set" of variables.  
* No OOP support planned, I'm keeping it simple, for now.  
  
### Examples  
(Bigger examples will be added at a later date)  
Hello world:  
```c
print("Hello world");
```  
Counting to ten:  
```c
c = 0;
while(c < 10)
{
    print("Counting: " + (++c));
}
```  
Declaring a function:  
```c
def add(a, b)
{
   return(a+b);
}
print("2+3=" + add(2,3));
```  
  
### To do  
* More and more optimizations (especially for the run part). Compilation speed is satisfying, for now. As a result, big changes to the code could still happen.  
* Test the compiler robustness (I might have missed some error cases).  
* Add some sort of array support.  
* Add some sort of variable set shared between all functions of a same script.  
* Code cleanup/rewrite where it's needed.  
* Debug mode/function(s) (?).  