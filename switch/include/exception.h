/*
 * This file is part of Chiaki.
 *
 * Chiaki is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Chiaki is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Chiaki.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CHIAKI_EXCEPTION_H
#define CHIAKI_EXCEPTION_H

#include <exception>

#include <string>

class Exception : public std::exception
{
	private:
		const char *msg;

	public:
		explicit Exception(const char *msg) : msg(msg) {}
		const char *what() const noexcept override { return msg; }
};

#endif // CHIAKI_EXCEPTION_H
