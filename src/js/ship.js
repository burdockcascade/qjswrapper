// Instantiate our C++ class from JS
const voyager = new Ship("Voyager One", 100);

console.log(`Ship Name: ${voyager.getName()}`);
console.log("Current Fuel:", voyager.fuel);

// Call C++ methods
voyager.fly(20);
voyager.fly(30);

console.log(`Final Fuel: ${voyager.getFuel()}`);
console.log("Fuel after flight:", voyager.x);

// This would throw a TypeError because of our engine's safety checks:
voyager.fly("fast");