# MusicReader

I wrote this program to solve specific issues I have with viewing PDFs while playing piano. These may not match your concerns or needs. Oh well, this is for me, I'm just releasing it because I'm sure at least some people have these concerns.

I wanted to solve the following problems:

## Scores displayed full page and as big as possible

* PDF readers make you zoom in and out, show partial pages, I hate fighting with all of that. This app always shows the full page.

* PDF readers have all kinds of toolbars, menus, and features, none which we need. I put my toolbar and tabs on the side, to reserve vertical space for the score. You can hide the menu, status bar, and toolbar, and even go fullscreen with F11.

* Shortcuts are single key, no CTRL or ALT needed. I can create a bookmark by pressing B, and immediately start typing the name of the bookmark. Search IMSLP by pressing I. Change view mode by pressing V. Zoom border by pressing Z. etc.

*  Sheet music often has wide borders, which are not needed on a computer. MusicReader detects this and zooms in to show only the music.

## Better Page Turns

We all have sheet music where the 8 bar menuet of a suite starts at the bottom of the right page, requiring a page turn. This is annoying, especially if you are playing a piece that fits on 2 pages. MusicReader has a 2 page mode that shows left/right pages side by side. However, there is an option that page up/down moves only 1 page at a time - move that minuet's start to the left, and now you can see the entire piece on one screen, no page turn required.

## IMSLP Integration

You can quickly search IMSLP for a piece by pressing I, and then download it to your computer by double clicking its thumbnail. It is opened in MusicReader and also saved to your hard drive. Much easier than using a web browser.

## Editing

This isn't well supported yet, but you can edit bookmarks. If you need to make bigger changes, you can open it in your system's default PDF editor with a right mouse click, and then when you have saved it in the external editor, press F5 to refresh the file in MusicReader (F5 - just like in a web browser).

## File Management

I don't like apps that put my files in a proprietary storage system. I also don't like navigating directories using explorer. MusicReader has a fast file search that indexes all your PDFs, and allows you to instantly  find the file you want. You can also use the standard Windows open file dialog if you prefer.

It is blazingly fast, and lets you type incomplete names. For example, if you want to find Bach's prelude 934, but don't quite remember what it is called, you can type 'bac 934' and it will match any file that has those char
acters in the name. So 'bac' will match 'Bach', and '934 will match 'bwv-934', 'prelude_934.pdf', etc. Search is applied to folder and file names, so sheet_music\bach\934-my-homework.pdf will match 'bach 934' as well.

## Future Plans

I do want to be able to add text annotations, and to have the concept of a playlist, which would be useful for a performance. Maybe.

## And Nothing Else

No phone home. No ads. No cloud. No features you don't need. No forced updates. No app store. It doesn't even "install", you justs unzip it wherever you want and run it. Delete the folder and it's gone.

Ask me to add a feature, I'll probably say no, even if you offer to write it. Lean and mean, just do the one thing of displaying sheet music.
