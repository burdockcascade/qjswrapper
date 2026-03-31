// Instantiate our C++ class from JS
const voyager = new Ship("Voyager", 30);

console.log(`Ship Name: ${voyager.getName()}`);
console.log("Current Fuel:", voyager.fuel);

voyager.refuel(50);

// Call C++ methods
voyager.fly(20);
voyager.fly(30);

console.log(`Final Fuel: ${voyager.getFuel()}`);
console.log("Fuel after flight:", voyager.x);