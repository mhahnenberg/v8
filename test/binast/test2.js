'use strict';

var deep = function deep() {
  var period = '.';
  return function inner(str) {
    return function innermost() {
      return str + period;
    };
  };
}

console.log(deep()('done')());