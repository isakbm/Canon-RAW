**Canon RAW** 
=========================

**In this project I attempt to decode Canon RAW, specifically .CR2 image files to difference values.**

![Brother](https://i.imgur.com/dyRIIxN.png)

The above picture was made by taking the raw .CR2 file, producing the difference values, and summing them up. The image that one gets from this has glitches in it for unknown reasons (this project is still not completed). Colors and contrast were added and adjusted in Gimp.

The console application takes two arguments.

`main.a <input> <output>`

`<input>`    name of the input Canon raw file, e.g image.CR2

`<output>`   name of the output file, e.g diff_values.dat

The output is a file containing raw difference values from which you can continue to construct an image by simply summing the difference values. 
Resetting at the start of every new row.

In addition to producing a raw difference value image, the application also prints out metadata and headers.
Something that may or may not be useful to you, but that was definitely useful for debugging.

This piece of code was only possible thanks to an invaluable source on the .CR2 format: http://lclevy.free.fr/cr2/

A very nice poster on the lossless JPEG encoding can be found here: https://github.com/lclevy/libcraw2/blob/master/docs/cr2_lossless.pdf?raw=true

That source explains how to interpret the Huffman encoding and how to extract the difference values from it, something that turns out to be a very fun exercise in itself.

Note that the output is raw difference values. A lot of extra processing is needed to get a picture. You need to integrate the difference values, and do demosaicing to reconstruct color. 
Demosaicing is necessary since, as you might know, digital cameras use a Bayer filter "[[R,G], [G,B]] * [res_x, res_y]".
