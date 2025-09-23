# MusicReader

I wrote this program to solve specific issues I have with viewing PDFs while playing piano. I have mild vision issues, and so it always display full pages as large as possible - no manual zooming, no large toolbars taking vertical space. And it should be very easy to use - no need for CTRL keys for shortcuts, no elaborate menus or dialogs - everything can be done with a single key press. Searching for files in your pdf collection should be instant. No update service, telemetry, embedded ads, nagware, involuntary email subscriptions. 

These may not match your concerns or needs. Oh well, this is for me, I'm just releasing it because I'm sure at least some people have these concerns. 


## Scores displayed full page and as big as possible

* Sheet music is tall and narrow, monitors are short and wide. Hence I prioritize using all the vertical space for the music only. Tabs for multiple documents, bookmarks, even the toolbar are placed on the left by default. You can even hide the menu and status bar to gain more height.

To expand on this - i wear glasses, and have a horizontal astigmitism. That makes ledger lines blurry by default. The larger and crisper the sheets, the easier sight reading is. Just shrinking the page a bit, while I'd say it is entirely legible, slows me down as it takes just a bit longer to process. 

*  Sheet music often has wide borders, which are not needed on a computer, as the app acts like the border. MusicReader has a mode that detects borders in sheet music and zooms in to show only the music (does not perform well on noisy scans of old, yellowed sheets).

* PDF readers make you zoom in and out, show partial pages, I hate fighting with all of that. This app always shows the full pagee.

* PDF readers have all kinds of toolbars, menus, and features, none which we need. I put my toolbar and tabs on the side, to reserve vertical space for the score. You can hide the menu, status bar, and toolbar, and go fullscreen with F11.

* Most shortcuts are single key, no CTRL or ALT needed (undo/redo use CTRL+Z/Y for compatibility). For example, create a bookmark by pressing B, and immediately start typing the name of the bookmark. Search IMSLP by pressing I. Change view mode by pressing V. Zoom to hide the border by pressing Z, and Z again to un-zoom. 


## Better Page Turns

MusicReader has a 2 page mode that shows left/right pages side by side. We all have sheet music where a 16 bar movement in a suite starts at the bottom of the right page, requiring an immediate page turn. There is no reason to emulate a bound book, and so MusicReader has options that lets you move forward one page at a time - put the start of the movement on the left page, and now you can see the entire piece on one screen, no page turn required. You can use the left/right arrow keys to advance by one page, or use the Page Step shortcut to toggle whether page up/down keys turn 1 or 2 pages at a time (in two page mode only, it is always 1 page if in single page mode).

## Editing

This isn't well supported yet, but you can edit bookmarks. If you need to make bigger changes, you can open it in your system's default PDF editor with a right mouse click, and then when you have saved it in the external editor, press F5 to refresh the file in MusicReader (F5 - just like in a web browser).

## Fast File Search and Management

I don't like apps that put my files in a proprietary storage system. I also don't like navigating directories using explorer. MusicReader has a fast file search that indexes all your PDFs, and allows you to instantly find the file you want. You can also use the standard Windows open file dialog if you prefer.

It is blazingly fast, and lets you type incomplete names. For example, if you want to find Bach's prelude 934, but don't quite remember what it is called, you can type 'bac 934' and it will match any file that has those characters in the name. So 'bac' will match 'Bach', and '934 will match 'bwv-934', 'prelude_934.pdf', etc. Search is applied to folder and file names, so sheet_music\bach\934-my-homework.pdf will match 'bach 934' as well.

## IMSLP Integration

You can quickly search IMSLP for a piece by pressing I, and then download it to your computer by double clicking its thumbnail. It is opened in MusicReader and also saved to your hard drive, defaulting to your music collection location used by the Fast File Search.

## Future Plans

I do want to be able to add text annotations, and to have the concept of a playlist, which would be useful for a performance. Support for a musical font to allow adding convience naturals, etc. A dream would be the ability to reflow systems across pages, I've worked on it but it is far from ready for deployment. It is not internationalized at all, though I have tested sheet music with non western characters in the file names. If others end up using it, a help system and perhaps a tutorial are needed.

## And Nothing Else

No phone home. No ads. No cloud. No features you don't need. No forced updates. No app store. It doesn't even "install", you justs unzip it wherever you want and run it. Delete the folder and it's gone.

Ask me to add a feature, I'll probably say no, even if you offer to write it. Lean and mean, just do the one thing of displaying sheet music.
