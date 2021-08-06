# Documentation

Go through this document to get familiar with how Tok code looks like, and what you can do with it.

**Important Note**: ctok doesn't yet support Classes and related OOP concepts, but Tok does. I'm actively working on this
project to get that working. If you'd like to check out a fully functional interpreter for Tok, please check out the one
I wrote in Java, called [jtok](https://github.com/JyotinderSingh/jtok)!

## Hello, Tok

```
// Your first Tok program.
print "Hello, world!";
```

### Dynamic Typing

Tok is dynamically typed. Variables can store values of any type, and a single variable can even store values of
different types at different times. If you try to perform an operation on values of the wrong type—say, dividing a
number by a string—then the error is detected and reported at runtime.

### Automatic Memory Managment

Tok includes its own Garbage Collector which makes use of **Mark-Sweep Garbage Collection**.

### Data Types

Booleans

```
true;
false;
```

Numbers

Tok only support one kind of Number type: double-precision floating-point, which can of course also represent integers.

```
1234;   // an integer.
12.34;  // a decimal number.
```

Strings

```
"I am a string";
"";     // an empty string.
```

Nil

Represents the *null* value equivalent of Tok.

### Expressions

Tok features the basic arithmetic operators similar to C and other languages

```
add + me;
subtract - me;
multiply * me;
divide / me;
```

Most of the operators are binary and infix, except a few:

```
-negateMe;
```

All of these operators only work on numbers, and it's an error to pass any other types to them. The exception is
the ```+``` operator, which can be used to concatenate two strings passed to it.

### Comparison and Equality

We have the following comparison operators:

```
less < than;
lessThan <= orEqual;
greater > than;
greaterThan >= orEqual;
```

We can test two values for any kind of inequality.

```
1 == 2;         // false.
"cat" != "dog"; // true.
```

Even different types.

```
314 == "pi"; // false.
```

Values of different types are *never* equivalent.

```
123 == "123";   // false
```

### Logical Operators

The *not* operator, a prefix ```!```, returns ```false``` if its operand is ```true```, and vice versa.

```
!true;  // false
!false; // true
```

An ```and``` expression determines if two values are both ```true```. It returns the left operand if it’s ```false```,
or the right operand otherwise.

```
true and false; // false.
true and true;  // true.
```

An ```or``` expression determines if either of two values (or both) are ```true```. It returns the left operand if it
is ```true```
and the right operand otherwise.

```
false or false; // false.
true or false;  // true.
```

```and``` and ```or``` are like flow structures because they short-circuit operators. Not only does ```and``` return the
left operand if it is
```false```, it doesn’t even evaluate the right one in that case. Conversely, if the left operand of an or is ```true```
, the right is skipped.

### Precedence and Grouping

The operators follow the same precedence and grouping as C. You can use paranthesis if you want to group stuff and not
rely on precedence.

### Statements

A expression’s main job is to produce a value, a statement’s job is to produce an effect. Statements don’t evaluate to a
value, to be useful they have to otherwise change something in some way—usually modifying some state, reading input, or
producing output.

```
"some expression";      // statement
print "Hello, world!";  // statement
```

An expression followed by a semicolon (;) promotes the expression to statement-hood.

If you want to pack a series of statements where a single one is expected, you can wrap them up in a block.

```
{
  print "One statement.";
  print "Two statements.";
}
```

### Variables

Variables are declared using the ```var``` keyword. If a variable is not initialized at definition, it defaults
to ```nill```.

```
var iAmAVariable = "some value";
var iAmNill;
var iAmANumber = 1234;

printi iAmVariable; // "some value"
```

### Control Flow

The language supports ```if``` statements.

```
if (condition) {
  print "yes";
} else {
  print "no";
}
```

```while``` loops.

```
var a = 1;
while (a < 10) {
  print a;
  a = a + 1;
}
```

```for``` loops.

```
for (var a = 1; a < 10; a = a + 1) {
  print a;
}
```

### Functions

Functions look similar to what they look in C.

```
makeBreakfast(bacon, eggs, toast)
```

Functions can also be called without passing anything to them.

```
makeBreakfast()
```

To define your own function, use the ```fun``` keyword.

```
fun printSum(a, b) {
  print a + b;
}
```

### Closures

Functions are *first class* in Tok, hence they have real values that you can reference to, store in variables, and pass
around.

```
fun addPair(a, b) {
  return a + b;
}

fun identity(a) {
  return a;
}

print identity(addPair)(1, 2); // Prints "3".
```

Since function declarations are statements, you can declare local functions inside another function.

```
fun outerFunction() {
  fun localFunction() {
    print "I'm local!";
  }

  localFunction();
}
```

If you combine local functions, first-class functions, and block scope, you run into this interesting situation:

```
fun returnFunction() {
  var outside = "outside";

  fun inner() {
    print outside;
  }

  return inner;
}

var fn = returnFunction();
fn();
```

Here, ```inner()``` accesses a local variable declared outside its body in the surrounding function.

For this to work, ```inner()``` has to “hold on” to references to any surrounding variables that it uses so that they
stay around even after the outer function has returned. This is made possible with *closures*.

### Classes

You can define a class and its methods like so:

```
class Breakfast {
  cook() {
    print "Eggs a-fryin'!";
  }

  serve(who) {
    print "Enjoy your breakfast, " + who + ".";
  }
}
```

The body of a class contains its methods. They look like function declarations but without the ```fun``` keyword.

Classes are also *first class* in Tok.

```
// Store it in variables.
var someVariable = Breakfast;

// Pass it to functions.
someFunction(Breakfast);
```

Here's how you instantiate classes:

```
var breakfast = Breakfast();
print breakfast; // "Breakfast instance".
```

Instantiation and initialization:
Tok, like other dynamically typed languages, lets you freely add properties onto objects.

```
breakfast.meat = "sausage";
breakfast.bread = "sourdough";
```

Assigning to a field creates it if it doesn’t already exist.

If you want to access a field or method on the current object from within a method, you can use the ```this``` keyword.

```
class Breakfast {
  serve(who) {
    print "Enjoy your " + this.meat + " and " +
        this.bread + ", " + who + ".";
  }

  // ...
}

```

Part of encapsulating data within an object is ensuring the object is in a valid state when it’s created. To do that,
you can define an initializer. If your class has a method named init(), it is called automatically when the object is
constructed. Any parameters passed to the class are forwarded to its initializer.

```
class Breakfast {
  init(meat, bread) {
    this.meat = meat;
    this.bread = bread;
  }

  // ...
}

var baconAndToast = Breakfast("bacon", "toast");
baconAndToast.serve("Dear Reader");
// "Enjoy your bacon and toast, Dear Reader."
```

### Inheritance

Tok supports single inheritance. When you declare a class, you can specify a class that it inherits from using a
less-than ```<``` operator.

```
class Brunch < Breakfast {
  drink() {
    print "How about a Bloody Mary?";
  }
}
```

Here, Brunch is the **derived class** or **subclass**, and Breakfast is the **base class** or **superclass**.

Every method defined in the superclass is also available to its subclasses.

```
var benedict = Brunch("ham", "English muffin");
benedict.serve("Noble Reader");
```

Even the ```init()``` method gets inherited. In practice, the subclass usually wants to define its own ```init()```
method too. But the original one also needs to be called so that the superclass can maintain its state. We need some way
to call a method on our own instance without hitting our own methods.

We can use ```super``` for that.

```
class Brunch < Breakfast {
  init(meat, bread, drink) {
    super.init(meat, bread);
    this.drink = drink;
  }
}
```

### The standard Library

Well, the standard library isn't realy big enough to be called a book, let alone a library.

But it does define a built-in ```print``` statement, and a ```clock()``` method that returns the number of seconds since
the program started.