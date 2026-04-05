// Instantiate and perform actions
const voyager = new Ship("Voyager", 10);
voyager.refuel(30);
voyager.fly(20);

// Export to global scope so C++ can validate the final state
globalThis.voyager = voyager;