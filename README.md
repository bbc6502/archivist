# Archivist

Archivist is a fuse filesystem to store multiple
copies of files in a format that allows for the
detection and correction of corrupt bits in the file.

## Storage structure

The file storage format is 512 byte blocks with
each block containing a 32 byte header and 480
bytes of data.

### Block header

The 32 byte header consists of:
 * 2 byte version (network byte order)
 * 2 byte data length (network byte order)
 * 20 byte SHA-1 hash of the data
 * 8 byte seed which is a random value per block

Network byte order is most significant byte first.
(MSB ... LSB)

## File storage locations

Each file is stored in two separate locations.
In the event of a corrupt block being detected
the uncorrupted version in the other file is
used to fix the corruption in the file.

There are two locations. The first location
is the primary location. The primary location
is repaired from the secondary location if corrupt.
The secondary location if repaired from the
primary location if corrupt.
In the event that the primary and secondary
blocks are different but not corrupt it is
assumed that the primary is the correct one
and the secondary is replaced with the primary
version.

It is recommended that each location is stored
on a different physical device.

## Invocation

```
archivist <mount-point> <primary-storage-location> <secondary-storage-location>
```

## Unmounting

```
umount <mount-point>
```

## License

MIT License
