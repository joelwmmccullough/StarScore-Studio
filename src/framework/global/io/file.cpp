/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "file.h"

#include <cstring>

#include "ioretcodes.h"

#ifndef NO_QT_SUPPORT
#include <QFile>
#endif

#include "log.h"

using namespace muse;
using namespace muse::io;

File::File(const path_t& filePath)
    : m_filePath(filePath)
{
}

//! StarScore Studio (1.26.0): files at least this big, opened read-only, are mapped instead of read whole
static constexpr int64_t MAP_FROM_BYTES = 32 * 1024 * 1024;

struct File::Mapped {
#ifndef NO_QT_SUPPORT
    QFile file;
#endif
    const uint8_t* data = nullptr;
    size_t size = 0;
};

File::~File()
{
    close();
    doClose();
}

void File::doClose()
{
    if (m_mapped) {
#ifndef NO_QT_SUPPORT
        if (m_mapped->data) {
            m_mapped->file.unmap(const_cast<uchar*>(m_mapped->data));
        }
        m_mapped->file.close();
#endif
        delete m_mapped;
        m_mapped = nullptr;
    }
}

const path_t& File::filePath() const
{
    return m_filePath;
}

bool File::exists() const
{
    return fileSystem()->exists(m_filePath);
}

bool File::exists(const path_t& filePath)
{
    return fileSystem()->exists(filePath);
}

bool File::remove(const path_t& filePath)
{
    return fileSystem()->remove(filePath);
}

bool File::copy(const path_t& src, const path_t& dst, bool replace)
{
    return fileSystem()->copy(src, dst, replace);
}

Ret File::readFile(const io::path_t& filePath, ByteArray& out)
{
    bool ok = fileSystem()->readFile(filePath, out);
    return make_ret(ok ? Err::NoError : Err::FSReadError);
}

Ret File::writeFile(const io::path_t& filePath, const ByteArray& data)
{
    return fileSystem()->writeFile(filePath, data);
}

bool File::setPermissionsAllowedForAll(const path_t& filePath)
{
    return fileSystem()->setPermissionsAllowedForAll(filePath);
}

bool File::remove()
{
    return fileSystem()->remove(m_filePath);
}

bool File::doOpen(OpenMode m)
{
    if (m == IODevice::WriteOnly) {
        Ret ret = fileSystem()->isWritable(m_filePath);
        if (!ret) {
            setError(ret.code(), ret.text());
        }

        return ret;
    }

    if (!exists()) {
        if (m == OpenMode::ReadOnly) {
            setError(int(Err::FSReadError), "Opening non-existent file called on a read-only mode");
            return false;
        } else {
            return true;
        }
    }

    doClose();
    m_data = ByteArray();

#ifndef NO_QT_SUPPORT
    if (m == OpenMode::ReadOnly) {
        auto mapped = new Mapped();
        mapped->file.setFileName(m_filePath.toQString());
        if (mapped->file.size() >= MAP_FROM_BYTES && mapped->file.open(QIODevice::ReadOnly)) {
            mapped->size = size_t(mapped->file.size());
            mapped->data = mapped->file.map(0, mapped->file.size());
            if (mapped->data) {
                m_mapped = mapped;
                return true;
            }
        }
        delete mapped;
    }
#endif

    Ret ret = fileSystem()->readFile(m_filePath, m_data);
    if (!ret) {
        setError(ret.code(), ret.text());
        return false;
    }

    return true;
}

size_t File::dataSize() const
{
    return m_mapped ? m_mapped->size : m_data.size();
}

const uint8_t* File::rawData() const
{
    return m_mapped ? m_mapped->data : m_data.constData();
}

bool File::resizeData(size_t size)
{
    if (m_mapped) {
        return false;   // read-only
    }
    m_data.resize(size);
    return true;
}

size_t File::writeData(const uint8_t* data, size_t len)
{
    std::memcpy(m_data.data() + pos(), data, len);
    Ret ret = fileSystem()->writeFile(m_filePath, m_data);
    if (!ret) {
        setError(ret.code(), ret.text());
        return 0;
    }
    return len;
}
