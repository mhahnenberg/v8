'use strict';

function Thing() {
  this.$ = {foo: null, bar: null};
}

Thing.prototype.ready = function() {
  this.$.foo = 'foo';
  this.$.bar = false;
};

function makeThing() {
  var t = new Thing();
  t.ready();
  return t;
}

function explode2(a,b,c,d,e,f){"use strict";e.exports=a;function a(){return b;}}

var double = function(x) { return x * 2; }
function triple(x) { return x * 3; }
function deep() {
  return function level0() {
    return function level1() {
    return function level2() {
    return function level3() {
    return function level4() {
    return function level5() {
    return function level6() {
    return function level7() {
    return function level8() {
    return function level9() {
    return function level10() {
    return function level11() {
    return function level12() {
    return function level13() {
    return function level14() {
    return function level15() {
    return function level16() {
      return 42;
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
    };
  };
}
function sumFor(n) {
  var result = 0;
  for (var i = 0; i < n; ++i) {
    result += i;
  }
  return result;
}
function sumForIn(arr) {
  var result = 0;
  for (var i in arr) {
    result += arr[i];
  }
  return result;
}
function sumWhile(n) {
  var result = 0;
  var i = 0;
  while (i < n) {
    result += i;
    i += 1;
  }
  return result;
}
function sumDoWhile(n) {
  var result = 0;
  var i = 0;
  do {
    result += i;
    i += 1;
  } while (i < n);
  return result;
}

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
  timerCallbacks.sort(function sortFunction(e1, e2) {
    return e2.deadline - e1.deadline;
  });
};
setTimeout = newSetTimeout;

function tickRunLoop(nextDeadline) {
  oldSetTimeout(function timeoutCallback() {
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
  console.log(deep()()()()()()()()()()()()()()()()()());
  console.log(sumFor(10));
  console.log(sumForIn([1,2,3,4,5,6,7,8,9,10]));
  console.log(sumWhile(10));
  console.log(sumDoWhile(10));
  console.log(makeThing());
  var a = 0;
  var b = 'sweet!';
  var c = 2;
  var d = 3;
  var e = {};
  console.log(explode2(a, b, c, d, e));
  console.log(e);
  console.log(e.exports());
}, 5000);

tickRunLoop();
