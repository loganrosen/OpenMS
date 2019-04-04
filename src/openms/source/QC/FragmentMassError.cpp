// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2018.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow$
// $Authors: Patricia Scheil, Swenja Wagner$
// --------------------------------------------------------------------------

#include <OpenMS/QC/FragmentMassError.h>
#include <include/OpenMS/ANALYSIS/OPENSWATH/DIAHelper.h>
#include <string>

namespace OpenMS
{
  void FragmentMassError::compute(MSExperiment& exp, FeatureMap& fmap)
  {
    //accumulates ppm errors over all first PeptideHits
    double accumulator_ppm{};

    //counts number of ppm errors
    UInt32 counter_ppm{};

    float rt_tolerance = 0.05;
    float mz_tolerance = 0.05; //should be userparameter //TODO

    if (!exp.isSorted())
    {
      exp.sortSpectra();
    }


    auto lam = [&exp, rt_tolerance, mz_tolerance, &accumulator_ppm, &counter_ppm](PeptideIdentification& pep_id)
    {
      if (pep_id.getHits().empty())
      {
        //Warn
        return;
      }

      //---------------------------------------------------------------------
      // FIND DATA FOR THEORETICAL SPECTRUM
      //---------------------------------------------------------------------

      //sequence
      AASequence seq = pep_id.getHits()[0].getSequence();

      //charge
      Int charge = pep_id.getHits()[0].getCharge(); //TODO

      //theoretical peak spectrum
      PeakSpectrum theo_spectrum;

      //---------------------------------------------------------------------
      // CREATE THEORETICAL SPECTRUM
      //---------------------------------------------------------------------

      //initialize a TheoreticalSpectrumGenerator
      TheoreticalSpectrumGenerator theo_gen;

      //get current parameters (default)
      Param theo_gen_settings = theo_gen.getParameters();

      //default: b- and y-ions?
      theo_gen_settings.setValue("add_a_ions", "true");
      //theo_settings.setValue("add_b_ions", "true");
      theo_gen_settings.setValue("add_c_ions", "true");
      theo_gen_settings.setValue("add_x_ions", "true");
      //theo_settings.setValue("add_y_ions", "true");
      theo_gen_settings.setValue("add_z_ions", "true");

      //store ion types for each peak
      //theo_settings.setValue("add_metainfo", "true");

      //set changed parameters
      theo_gen.setParameters(theo_gen_settings);

      //generate a-, b- and y-ion spectrum of peptide seq with charge
      theo_gen.getSpectrum(theo_spectrum, seq, charge, charge);

      //-----------------------------------------------------------------------
      // GET EXPERIMENTAL SPECTRUM MATCHING TO PEPTIDEIDENTIFICTION
      //-----------------------------------------------------------------------

      double rt_pep  = pep_id.getRT();

      MSExperiment::ConstIterator it = exp.RTBegin(rt_pep - rt_tolerance);
      if (it == exp.end())
      {
        throw Exception::IllegalArgument(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, "The retention time of the mzML and featureXML fie does not match.");
      }

      const auto& exp_spectrum = *it;

      if (exp_spectrum.getRT() - rt_pep > rt_tolerance)
      {
        throw Exception::IllegalArgument(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, "PeptideID with RT " + std::to_string(rt_pep) + " s does not have a matching MS2 Spectrum. Closest RT was " + std::to_string(exp_spectrum.getRT()) + ", which seems to far off.");
      }
      if (exp_spectrum.getMSLevel() != 2)
      {
        throw Exception::IllegalArgument(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, "The matching retention time of the mzML is not a MS2 Spectrum.");
      }

      //-----------------------------------------------------------------------
      // COMPARE THEORETICAL AND EXPERIMENTAL SPECTRUM
      //-----------------------------------------------------------------------

      if (exp_spectrum.empty() || theo_spectrum.empty())
      {
        LOG_WARN << "The spectrum with " + std::to_string(exp_spectrum.getRT()) + " is empty." << "\n";
        return;
      }

      //stores ppms for one spectrum
      DoubleList ppms{};

      for (const Peak1D& peak : theo_spectrum)
      {
        const double theo_mz = peak.getMZ();
        double max_dist_dalton = theo_mz * mz_tolerance * 1e-6; // or ask for mz_tolerance in Dalton

        Size index = exp_spectrum.findNearest(theo_mz);
        const double exp_mz = exp_spectrum[index].getMZ();

        //found peak match
        if (std::abs(theo_mz - exp_mz) < max_dist_dalton)
        {
          float ppm = theo_mz - exp_mz;
          ppms.push_back(ppm);
          accumulator_ppm += ppm;
          ++ counter_ppm;
        }
      }

      //-----------------------------------------------------------------------
      // WRITE PPM ERROR IN PEPTIDEHIT
      //-----------------------------------------------------------------------

      pep_id.getHits()[0].setMetaValue("ppm_errors", ppms);
    };

    QCBase::iterateFeatureMap(fmap, lam);
    average_ppm_ = accumulator_ppm/counter_ppm;
  }


  float FragmentMassError::getResults() const
  {
    return average_ppm_;
  }


  QCBase::Status FragmentMassError::requires() const
  {
    return QCBase::Status() | QCBase::Requires::RAWMZML | QCBase::Requires::POSTFDRFEAT;
  }
} //namespace OpenMS