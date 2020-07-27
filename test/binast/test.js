'use strict';

var double = function(x) { return x * 2; }
function triple(x) { return x * 3; }

var oldSetTimeout = setTimeout;
var timerCallbacks = [];
function newSetTimeout(func, delayMs) {
  var deadline = new Date();
  deadline.setTime(deadline.getTime() + delayMs)
  var newCallback = new Object();
  newCallback.cb = func;
  newCallback.deadline = deadline;
  timerCallbacks.push(newCallback);
  // Reverse sort so back of the array has the nearest timer callbacks
  timerCallbacks.sort(function(e1, e2) {
    return e2.deadline - e1.deadline;
  });
};
setTimeout = newSetTimeout;

function tickRunLoop(nextDeadline) {
  oldSetTimeout(function() {
    // We don't currently have a deadline, so look for the next timer callback to find a new deadline.
    if (nextDeadline === null || nextDeadline === undefined) {
      // We're out of timer callbacks, so there's no more deadlines and we can now exit.
      if (timerCallbacks.length === 0) {
        return;
      }
      var nextCallback = timerCallbacks[timerCallbacks.length - 1];
      nextDeadline = nextCallback.deadline;
    }
   
    // We have a deadline, so check if we've hit it. 
    var currentTime = new Date();
    if (currentTime.getTime() < nextDeadline.getTime()) {
      // We haven't hit the deadline, so just tick the run loop.
      tickRunLoop(nextDeadline);
      return;
    }

    // We hit the deadline, so grab the callback and run it, then tick the run loop.
    var cb = timerCallbacks.pop();
    cb.cb();
    tickRunLoop();
  }, 1);
}

setTimeout(function testCallback2() {
  console.log("running callback");
  console.log(double(42));
  console.log(triple(24));
}, 5000);

tickRunLoop();
