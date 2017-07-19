/*
 * GeohazardVis plug-in: pCDM Modeling
 * Copyright (C) 2017 Karsten Tausche <geodev@posteo.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCommandLineParser>
#include <QApplication>

#include <gtest/gtest.h>


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    
    QCommandLineParser cmdParser;
    const QCommandLineOption waitAfterFinishedOption("waitAfterFinished");
    cmdParser.addOption(waitAfterFinishedOption);
    cmdParser.parse(app.arguments());

    ::testing::InitGoogleTest(&argc, argv);

    auto result = RUN_ALL_TESTS();

    if (cmdParser.isSet(waitAfterFinishedOption))
    {
        getchar();
    }

    return result;
}
