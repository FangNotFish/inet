/***************************************************************************
 * author:      Oliver Graute, Andreas Kuntz, Felix Schmidt-Eisenlohr
 *
 * copyright:   (c) 2008 Institute of Telematics, University of Karlsruhe (TH)
 *
 * author:      Alfonso Ariza
 *              Malaga university
 *
 *              This program is free software; you can redistribute it
 *              and/or modify it under the terms of the GNU General Public
 *              License as published by the Free Software Foundation; either
 *              version 2 of the License, or (at your option) any later
 *              version.
 *              For further information see file COPYING
 *              in the top level directory
 ***************************************************************************/

#include "RayleighFading.h"

using namespace physicallayer;

Register_Class(RayleighFading);

void RayleighFading::printToStream(std::ostream &stream) const
{
    stream << "Rayleigh fading, "
           << "alpha = " << alpha << ", "
           << "system loss = " << systemLoss;
}

double RayleighFading::computePathLoss(mps propagationSpeed, Hz carrierFrequency, m distance) const
{
    m waveLength = propagationSpeed / carrierFrequency;
    double freeSpacePathLoss = computeFreeSpacePathLoss(waveLength, distance, alpha, systemLoss);
    double x = normal(0, 1);
    double y = normal(0, 1);
    return freeSpacePathLoss * 0.5 * (x * x + y * y);
}
