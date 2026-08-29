
#include "archive.hpp"


namespace Archives
{

ConstrainedFileStreamBuf::ConstrainedFileStreamBuf(std::unique_ptr<std::istream> file, std::streamsize start, std::streamsize end)
  : mStart(start), mEnd(end), mFile(std::move(file))
{
}
ConstrainedFileStreamBuf::~ConstrainedFileStreamBuf()
{
}

ConstrainedFileStreamBuf::int_type ConstrainedFileStreamBuf::underflow()
{
    if(gptr() == egptr())
    {
        // off_type on both sides: mEnd is a streamsize and tellg() returns
        // fpos<mbstate_t>, and mixing the two is ambiguous under libc++.
        const std::streamsize left =
            std::streamsize(off_type(mEnd) - off_type(mFile->tellg()));
        std::streamsize toread = std::min<std::streamsize>(left, mBuffer.size());
        mFile->read(mBuffer.data(), toread);
        setg(mBuffer.data(), mBuffer.data(), mBuffer.data()+mFile->gcount());
    }
    if(gptr() == egptr())
        return traits_type::eof();

    return traits_type::to_int_type(*gptr());
}

ConstrainedFileStreamBuf::pos_type ConstrainedFileStreamBuf::seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode)
{
    if((mode&std::ios_base::out) || !(mode&std::ios_base::in))
        return traits_type::eof();

    // New file position, relative to mOrigin. Kept as off_type (a plain
    // integer) rather than streampos: pos_type is fpos<mbstate_t>, which
    // defines its own arithmetic operators, so mixing it with streamsize is
    // ambiguous under libc++ even though libstdc++ accepts it.
    off_type newPos = 0;
    switch(whence)
    {
        case std::ios_base::beg:
            newPos = offset + off_type(mStart);
            break;
        case std::ios_base::cur:
            newPos = offset + off_type(mFile->tellg()) - off_type(egptr()-gptr());
            break;
        case std::ios_base::end:
            newPos = offset + off_type(mEnd);
            break;
        default:
            return traits_type::eof();
    }

    if(newPos < off_type(mStart) || newPos > off_type(mEnd))
        return traits_type::eof();

    if(!mFile->seekg(newPos))
        return traits_type::eof();

    // Clear read pointers so underflow() gets called on the next read attempt.
    setg(0, 0, 0);

    return pos_type(newPos - off_type(mStart));
}

ConstrainedFileStreamBuf::pos_type ConstrainedFileStreamBuf::seekpos(pos_type pos, std::ios_base::openmode mode)
{
    if((mode&std::ios_base::out) || !(mode&std::ios_base::in))
        return traits_type::eof();

    const off_type off = off_type(pos);
    if(off < 0 || off > off_type(mEnd - mStart))
        return traits_type::eof();

    if(!mFile->seekg(off + off_type(mStart)))
        return traits_type::eof();

    // Clear read pointers so underflow() gets called on the next read attempt.
    setg(0, 0, 0);

    return pos;
}


} // namespace Archives
