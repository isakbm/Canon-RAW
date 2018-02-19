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

A very nice .pdf poster on the lossless JPEG encoding can be found here: https://github.com/lclevy/libcraw2/blob/master/docs/cr2_lossless.pdf?raw=true

That source explains how to interpret the Huffman encoding and how to extract the difference values from it, something that turns out to be a very fun exercise in itself.

Here's the implementation of huffCodes. It takes the huffData from the .CR2 header and creates the decoding table:

```cpp
void huffCodes(uint8_t * huffData, uint16_t * table)
{
    // [0,1] then shift << 1 and add 0 , 1 repeat
    int tabCount = 0;
    int numNotUsed = 2;
    int numUsed    = 0;
    uint16_t unusedCodes [300];
    uint16_t usedCodes   [300];
    uint16_t temp        [300];
    unusedCodes[0] = 0;
    unusedCodes[1] = 1;

    for (int i = 0; i < 16; i++)
    {
        // Use a number of unused codes
        int num = huffData[i];
        for (int j = 0; j < num; j++)
            usedCodes[numUsed + j] = unusedCodes[j];

        // Remove those codes from unused codes list
        numUsed += num;
        for (int j = num; j < numNotUsed; j++)
            temp[j-num] = unusedCodes[j];
        numNotUsed -= num;

        // Update new codes
        for (int j = 0; j < numNotUsed; j++)
        {
            unusedCodes[2*j + 0] = (temp[j]<<1) + 0;
            unusedCodes[2*j + 1] = (temp[j]<<1) + 1;
        }
        numNotUsed *= 2;
    }

    for (int i = 0; i < numUsed; i++)
    {
        table[i] = usedCodes[i];
    }
}
```

Note that the output is a matrix of raw difference values. A lot of extra processing is needed to get a picture. You need to integrate the difference values, and do demosaicing to reconstruct color. 
Demosaicing is necessary since, as you might know, digital cameras use a Bayer filter "[[R,G], [G,B]] * [res_x, res_y]".
