.pragma library

// Blend `from` toward `to`; ratio 0 keeps `from`, 1 returns `to`.
//
// Hover and pressed states cannot be built from Qt.lighter()/Qt.darker() here:
// those scale HSV value, which barely moves a near-black wallpaper colour such
// as #090c10. Mixing toward the ink colour instead gives every palette the same
// visible amount of feedback.
function mix(from, to, ratio) {
    var keep = 1 - ratio;
    return Qt.rgba(from.r * keep + to.r * ratio,
                   from.g * keep + to.g * ratio,
                   from.b * keep + to.b * ratio,
                   1);
}

function luminance(color) {
    return 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
}

// Pick whichever of the two reads better on top of `background`.
function readableOn(background, first, second) {
    var page = luminance(background);
    return Math.abs(luminance(first) - page) >= Math.abs(luminance(second) - page)
        ? first : second;
}
