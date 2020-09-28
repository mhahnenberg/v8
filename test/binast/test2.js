'use strict';

var deep = function () {
  // var period = '.';
  return function inner() {
      return (function() {
        return 'done';
      });
  };
}

console.log(deep()()());