# Hyprscroller

[Hyprscroller](https://github.com/dawsers/hyprscroller) is a
[Hyprland](https://hyprland.org) layout plugin that creates a window layout
similar to PaperWM [PaperWM](https://github.com/paperwm/PaperWM). I initialy
learned how to write a *Hyprland* plugin from [hyprslidr](https://gitlab.com/magus/hyprslidr),
which is a similar idea.

![Intro](./videos/hyprscroller.gif)

The plugin is quite feature complete and supports gaps, borders, special
workspace, full screen modes, overview and installation through `hyprpm`.

I use *hyprscroller* on my main machine and will support it for as long as I
keep on using *Hyprland*. However, I will only add new features that I find
interesting, and support two *Hyprland* versions: the one my distribution uses,
and the latest tagged one. I have found problematic compiling trunk versions
of *Hyprland* on a system that already has a system-wide version of it
installed, so I will not make an extra effort there until things improve in
that front.


## Requirements

*hyprscroller* supports the version of *Hyprland* I use (currently v0.35)
plus the latest tagged one (currently v0.38.1), and you can try your luck with
the latest `git` changes, but I will be slower to keep up with those, as there
are too many API changes going on upstream.


## Building and installing

With Hyprland installed it should be as simple as running

``` sh
# builds a shared object hyprscroller.so
make all
# installs the shared library in ~/.config/hypr/plugins
make install
```

A more automated mode is to use `hyprpm`.

``` sh
hyprpm add https://github.com/dawsers/hyprscroller
# verify it installed correctly
hyprpm list
```

You can enable or disable it via `hyprpm enable hyprscroller` and
`hyprpm disable hyprscroller`.


## Configuration

If you are not using `hyprpm`, to make Hyprland load the plugin, add this to
your configuration.

``` conf
plugin = ~/.config/hypr/plugins/hyprscroller.so
```

Instead, if you use `hyprpm`, it should be as simple as adding this to
your `~/.config/hypr/hyprland.conf` :

``` conf
exec-once = hyprpm reload -n
```

To turn on the layout, use

``` conf
general {
    ...

    layout = scroller

    ...
}
```


## Dispatchers

The plugin adds the following dispatchers:

| Dispatcher                | Description                                                                                                                 |
|---------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| `scroller:movefocus`      | A replacement for `movefocus`, takes a direction as argument.                                                               |
| `scroller:movewindow`     | A replacement for `movewindow`, takes a direction as argument.                                                              |
| `scroller:setmode`        | Set mode: `r/row` (default), `c/col/column`. Sets the working mode. Affects most dispatchers and new window creation.       |
| `scroller:cyclesize`      | Resize the focused column width (*row* mode), or the active window height (*column* mode).                                  |
| `scroller:alignwindow`    | Align window on the screen, `l/left`, `c/center`, `r/right` (*row* mode), `c/center`, `u/up`, `d/down` (*col* mode)         |
| `scroller:admitwindow`    | Push the current window below the active one of the column to its left.                                                     |
| `scroller:expelwindow`    | Pop the current window out of its column and place it on a new column to the right.                                         |
| `scroller:fitsize`        | Resize columns (*row* mode) or windows (*col* mode) so they fit on the screen: `active`, `visible`, `all`, `toend`, `tobeg` |
| `scroller:toggleoverview` | Toggle an overview of the workspace where all the windows are temporarily scaled to fit the monitor                         |


## Modes

*Hyprscroller* works in any of two modes that can be changed at any moment.

1. *row* mode: it is the default. It creates new windows in a new column.
   `cyclesize` affects the width of the active column. `alignwindow` aligns
   the active column according to the argument received. `fitsize` fits the
   selected columns to the width of the monitor.

2. *column* mode: It creates new windows in the current column, right below the
   active window. `cyclesize` affects the height of the active window.
   `alignwindow` aligns the active window within the column, according to the
   argument received. `fitsize` fits the selected windows in the column to the
   height of the monitor.


## Window/Column Focus and Movement

If you want to use *Hyprscroller* you will need to map your key bindings from
the default `movefocus`/`movewindow` to
`scroller:movefocus`/`scroller:movewindow`. The reason is *Hyprland* doesn't
have the concept (yet) of a workspace that spans more than the space of a
monitor, and when focusing directionally, it doesn't look for windows that are
"outside" of that region. If this changes in the future, these two dispatches
may become obsolete.

`movefocus` and `movewindow` accept the following directional arguments:
`l` or `left`, `r` or `right`, `u` or `up`, `d` or `dn` or `down`, `b` or
`begin` or `beginning`, `e` or `end`. So you can focus or move windows/columns
in a direction or to the beginning or end or the row.


## Resizing

`cyclesize` accepts an argument which is either `+1`/`1`/`next`, or
`-1`/`prev`/`previous`. It cycles forward or backward through three column
widths (in *row* mode): one third, one half or two thirds of the available
width of the monitor. In *column* mode, the fractions are relative to the
height of the monitor, and are: one third, one half, two thirds or one.
However, using `resizewindow`, you can modify the width or height of
any window freely.


## Aligning

Columns are generally aligned in automatic mode, always making the active one
visible, and trying to make at least the previously focused one visible too if
it fits the viewport, if not, the one adjacent on the other side. However, you
can always align any column to the *center*, *left* or *right* of the monitor
(in *row* mode), or *up* (top), *down* (bottom) or to the *center* in *column*
mode. For example center a column for easier reading, regardless of what happens to
the other columns. As soon as you change focus or move a column, the alignment
is lost.

`alignwindow` takes a parameter: `l` or `left`, `r` or `right`, `c` or
`center` or `centre`, `u` or `up` and `d` or `down`.

To use *right* or *left* you need to be in *row* mode, and to use *up* or
*down* in *column* mode. *center* behaves differently depending on the mode.
In *row* mode it aligns the active column to the center of the monitor. In
*column* mode, it aligns the active window within its column, to a centered
position.


## Admit/Expel

You can create columns of windows using `admitwindow`. It takes the active
window and moves it to the column left of its current one, right under the
active window in that column.

To expel any window from its current column and position it in a new column to
its right, use `expelwindow`.


## Fitting the Screen

When you have a ultra-wide monitor, one in a vertical position, or the default
column widths or window heights don't fit your workflow, you can use manual
resizing, but it is sometimes slow and tricky.

`scroller:fitsize` works in two different ways, depending on the active mode.

It allows you to re-fit the columns (*row* mode) or windows (*column* mode) you
want to the screen extents. It accepts an argument related to the
columns/windows it will try to fit. The new width/height of each column/window
will be proportional to its previous width or height, relative to the other
columns or windows affected.

1. `active`: It is similar to maximize, it will fit the active column/window.
2. `visible`: All the currently fully or partially visible columns/windows will
   be resized to fit the screen.
3. `all`: All the columns in the row or windows in the column will be resized
   to fit.
4. `toend`: All the columns or windows from the focused one to the end of the
   row/column will be affected.
5. `tobeg` or `tobeginning`: All the columns/windows from the focused one to
   the beginning of the row/column will now fit the screen.


## Overview

`scroller:toggleoverview` toggles a bird's eye view of the current workspace where
all the windows are scaled to fit the current monitor. You can still interact
with them normally (change focus, move windows, type in them etc.). When
toggling back to normal mode, the original window sizes will be restored...so
it is not wise to use *toggleoverview* for window resizing or creating new windows.
Use it as a way to see where things are and move the active focus, or a window,
anything beyond that will probably find bugs or **cause compositor crashes**.


## Options

*hyprscroller* currently accepts the following options:

1. `column_default_width`: determines the width of new columns in *row* mode.
Possible arguments are: `onehalf` (default), `onethird`, `twothirds`,
`maximized`, `floating` (uses the default width set by the application).

For example:

``` conf
plugin {
    scroller {
        column_default_width = onehalf
    }
}
```


## Key bindings

As an example, you could set some key bindings in your `hyprland.conf` like this:

``` conf
# Move focus with mainMod + arrow keys
bind = $mainMod, left, scroller:movefocus, l
bind = $mainMod, right, scroller:movefocus, r
bind = $mainMod, up, scroller:movefocus, u
bind = $mainMod, down, scroller:movefocus, d
bind = $mainMod, home, scroller:movefocus, begin
bind = $mainMod, end, scroller:movefocus, end

# Movement
bind = $mainMod CTRL, left, scroller:movewindow, l
bind = $mainMod CTRL, right, scroller:movewindow, r
bind = $mainMod CTRL, up, scroller:movewindow, u
bind = $mainMod CTRL, down, scroller:movewindow, d
bind = $mainMod CTRL, home, scroller:movewindow, begin
bind = $mainMod CTRL, end, scroller:movewindow, end

# Modes
bind = $mainMod, semicolon, scroller:setmode, row
bind = $mainMod, apostrophe, scroller:setmode, col

# Sizing keys
bind = $mainMod, equal, scroller:cyclesize, next
bind = $mainMod, minus, scroller:cyclesize, prev

# Admit/Expel
bind = $mainMod, I, scroller:admitwindow,
bind = $mainMod, O, scroller:expelwindow,

# Center submap
# will switch to a submap called center
bind = $mainMod, C, submap, center
# will start a submap called "center"
submap = center
# sets repeatable binds for resizing the active window
bind = , C, scroller:alignwindow, c
bind = , C, submap, reset
bind = , right, scroller:alignwindow, r
bind = , right, submap, reset
bind = , left, scroller:alignwindow, l
bind = , left, submap, reset
bind = , up, scroller:alignwindow, u
bind = , up, submap, reset
bind = , down, scroller:alignwindow, d
bind = , down, submap, reset
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# Resize submap
# will switch to a submap called resize
bind = $mainMod SHIFT, R, submap, resize
# will start a submap called "resize"
submap = resize
# sets repeatable binds for resizing the active window
binde = , right, resizeactive, 100 0
binde = , left, resizeactive, -100 0
binde = , up, resizeactive, 0 -100
binde = , down, resizeactive, 0 100
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# Fit size submap
# will switch to a submap called fitsize
bind = $mainMod, W, submap, fitsize
# will start a submap called "fitsize"
submap = fitsize
# sets binds for fitting columns/windows in the screen
bind = , W, scroller:fitsize, visible
bind = , W, submap, reset
bind = , right, scroller:fitsize, toend
bind = , right, submap, reset
bind = , left, scroller:fitsize, tobeg
bind = , left, submap, reset
bind = , up, scroller:fitsize, active
bind = , up, submap, reset
bind = , down, scroller:fitsize, all
bind = , down, submap, reset
# use reset to go back to the global submap
bind = , escape, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset

# overview keys
# bind key to toggle overview (normal)
bind = $mainMod, tab, scroller:toggleoverview
# overview submap
# will switch to a submap called overview
bind = $mainMod, tab, submap, overview
# will start a submap called "overview"
submap = overview
bind = , right, scroller:movefocus, right
bind = , left, scroller:movefocus, left
bind = , up, scroller:movefocus, up
bind = , down, scroller:movefocus, down
# use reset to go back to the global submap
bind = , escape, scroller:toggleoverview,
bind = , escape, submap, reset
bind = , return, scroller:toggleoverview,
bind = , return, submap, reset
bind = $mainMod, tab, scroller:toggleoverview,
bind = $mainMod, tab, submap, reset
# will reset the submap, meaning end the current one and return to the global one
submap = reset
```

