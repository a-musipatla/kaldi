// ivectorbin/ivector-compute-lda.cc

// Copyright 2013  Daniel Povey

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.


#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "gmm/am-diag-gmm.h"
#include "ivector/ivector-extractor.h"
#include "util/kaldi-thread.h"

namespace kaldi {


class CovarianceStats {
 public:
  CovarianceStats(int32 dim): tot_covar_(dim),
                              between_covar_(dim),
                              between_covar_weighted_(dim),
                              within_covar_weighted_(dim),
                              num_spk_(0),
                              num_utt_(0) { }

  /// get total covariance, normalized per number of frames.
  void GetTotalCovar(SpMatrix<double> *tot_covar) const {
    KALDI_ASSERT(num_utt_ > 0);
    *tot_covar = tot_covar_;
    tot_covar->Scale(1.0 / num_utt_);
  }
  void GetWithinCovar(SpMatrix<double> *within_covar) {
    KALDI_ASSERT(num_utt_ - num_spk_ > 0);
    *within_covar = tot_covar_;
    within_covar->AddSp(-1.0, between_covar_);
    within_covar->Scale(1.0 / num_utt_);
  }
  void GetWithinCovarWeighted(SpMatrix<double> *within_covar_weighted) {
    KALDI_ASSERT(num_utt_ - num_spk_ > 0);
    *within_covar_weighted = within_covar_weighted_;
  }
  void GetBetweenCovarWeighted(SpMatrix<double> *between_covar_weighted) {
    KALDI_ASSERT(num_utt_ - num_spk_ > 0);
    *between_covar_weighted = between_covar_weighted_;
    // scale by 1/N
    between_covar_weighted->Scale(1.0 / num_utt_);
  }
  void AccStats(const Matrix<double> &utts_of_this_spk) {
    int32 num_utts = utts_of_this_spk.NumRows();
    tot_covar_.AddMat2(1.0, utts_of_this_spk, kTrans, 1.0);
    Vector<double> spk_average(Dim());
    spk_average.AddRowSumMat(1.0 / num_utts, utts_of_this_spk);
    between_covar_.AddVec2(num_utts, spk_average);
    num_utt_ += num_utts;
    num_spk_ += 1;
  }
  // Update between_covar_weighted_ 
  void AccWeightedStats(const Matrix<double> &utts_of_spk_i, 
                        int32 n_i, 
                        const Matrix<double> &utts_of_spk_j, 
                        int32 n_j, 
                        int32 lda_var, 
                        int32 wlda_n) {
    //KALDI_LOG << "\t\t\tRunning AccWeightedStats";

    // calculate average ivector for speaker i
    Vector<double> spk_i_average(Dim());
    spk_i_average.AddRowSumMat(1.0 / n_i, utts_of_spk_i);
    // calculate average ivector for speaker j
    Vector<double> spk_j_average(Dim());
    spk_j_average.AddRowSumMat(1.0 / n_j, utts_of_spk_j);
    // calculate (w_i - w_j)
    Vector<double> spk_diff(Dim());
    spk_diff.AddVec(1.0, spk_i_average);
    spk_diff.AddVec(-1.0, spk_j_average);
    // calculate the w(d_ij)
    double w = 1;
    if (lda_var == 1) {
      w = euclidean_distance_weight(w, spk_diff, wlda_n);
    } else if (lda_var == 2){
      w = mahalanobis_distance_weight(w, spk_diff, wlda_n);
    }
    // calculate w(d_ij) n_i n_j
    double weight = w * n_i * n_j;
    
    // calculate w(d_ij) n_i n_j (w_i - w_j)(w_i - w_j)^T and add to covar
    between_covar_weighted_.AddVec2(weight, spk_diff);
  }
  /// Will return Empty() if the within-class covariance matrix would be zero.
  bool SingularTotCovar() { return (num_utt_ < Dim()); }
  bool Empty() { return (num_utt_ - num_spk_ == 0); }
  std::string Info() {
    std::ostringstream ostr;
    ostr << num_spk_ << " speakers, " << num_utt_ << " utterances. ";
    return ostr.str();
  }
  // Update within_covar_weighted_ 
  void AccWeightedStatsWithin(const Matrix<double> &utts_of_spk) {
    //KALDI_LOG << "\t\t\tRunning AccWeightedStats";
    
    // number of utterances for this speaker
    int32 num_utts = utts_of_spk.NumRows();

    // calculate average vector (w_s) for speaker
    Vector<double> spk_average(Dim());
    spk_average.AddRowSumMat(1.0 / num_utts, utts_of_spk);

    // calculate SUM_i=1^n_s (w_i^s - w_s)(w_i^s - w_s)^T
    Vector<double> utt(Dim());
    for (int32 n = 0; n < num_utts; n++) {
      utt.CopyFromVec(utts_of_spk.Row(n));
      utt.AddVec(-1.0, spk_average);
      within_covar_weighted_.AddVec2(1.0, utt);
    }
  }
  int32 Dim() { return tot_covar_.NumRows(); }
  // Use default constructor and assignment operator.
  void AddStats(const CovarianceStats &other) {
    tot_covar_.AddSp(1.0, other.tot_covar_);
    between_covar_.AddSp(1.0, other.between_covar_);
    num_spk_ += other.num_spk_;
    num_utt_ += other.num_utt_;
  }
 private:
  KALDI_DISALLOW_COPY_AND_ASSIGN(CovarianceStats);
  SpMatrix<double> tot_covar_;
  SpMatrix<double> between_covar_;
  SpMatrix<double> between_covar_weighted_;
  SpMatrix<double> within_covar_weighted_;
  int32 num_spk_;
  int32 num_utt_;

  double euclidean_distance_weight(double w, 
                                 Vector<double> spk_diff, 
                                 int32 n) {
    // Euclidean distance weighting is defined as:
    //    w(d_ij) = ((w_i − w_j)^T (w_i − w_j))^−n
    // where
    //    n:   can be selected as anything

    // dot product of (w_1 - w_j)
    //KALDI_LOG << "\t\t\tRunning Euclidean Distance Weight";
    w = VecVec(spk_diff, spk_diff);
    // take dot product to power of -n
    return pow(w,-n);
  }
  double mahalanobis_distance_weight(double w, 
                                   Vector<double> spk_diff, 
                                   int32 n) {
    // Mahalanobis distance weighting is defined as:
    //    w(d_ij) = ((w_i − wIj)^T (S_w)^-1 (w_i − w_j))^−n
    // where
    //    n:   can be selected as anything
    //KALDI_LOG << "\t\t\tRunning Mahalanobis Distance Weight";
    SpMatrix<double> within_covar;
    GetWithinCovarWeighted(&within_covar);
    within_covar.Invert();
    Matrix<double> within_covar_mat(within_covar);
    Vector<double> spk_diff_times_covar(spk_diff);
    spk_diff_times_covar.AddMatVec(1.0, within_covar_mat, kTrans, spk_diff, 0.0);
    w = VecVec(spk_diff_times_covar, spk_diff);
    return pow(w,-n);
  }

};


template<class Real>
void ComputeNormalizingTransform(const SpMatrix<Real> &covar,
                                 Real floor,
                                 MatrixBase<Real> *proj) {
  int32 dim = covar.NumRows();
  Matrix<Real> U(dim, dim);
  Vector<Real> s(dim);
  covar.Eig(&s, &U);
  // Sort eigvenvalues from largest to smallest.
  SortSvd(&s, &U);
  // Floor eigenvalues to a small positive value.
  int32 num_floored;
  floor *= s(0); // Floor relative to the largest eigenvalue
  s.ApplyFloor(floor, &num_floored);
  if (num_floored > 0) {
    KALDI_WARN << "Floored " << num_floored << " eigenvalues of covariance "
               << "to " << floor;
  }
  // Next two lines computes projection proj, such that
  // proj * covar * proj^T = I.
  s.ApplyPow(-0.5);
  proj->AddDiagVecMat(1.0, s, U, kTrans, 0.0);
}

void ComputeLdaTransform(
    const std::map<std::string, Vector<BaseFloat> *> &utt2ivector,
    const std::map<std::string, std::vector<std::string> > &spk2utt,
    BaseFloat total_covariance_factor,
    BaseFloat covariance_floor,
    int32 lda_variation,
    int32 wlda_n,
    MatrixBase<BaseFloat> *lda_out) {
  KALDI_ASSERT(!utt2ivector.empty());
  int32 lda_dim = lda_out->NumRows(), dim = lda_out->NumCols();
  KALDI_ASSERT(dim == utt2ivector.begin()->second->Dim());
  KALDI_ASSERT(lda_dim > 0 && lda_dim <= dim);

  CovarianceStats stats(dim);

  // Standard LDA calculations
  std::map<std::string, std::vector<std::string> >::const_iterator iter;
  for (iter = spk2utt.begin(); iter != spk2utt.end(); ++iter) {
    const std::vector<std::string> &uttlist = iter->second;
    KALDI_ASSERT(!uttlist.empty());

    int32 N = uttlist.size(); // number of utterances.
    Matrix<double> utts_of_this_spk(N, dim);
    for (int32 n = 0; n < N; n++) {
      std::string utt = uttlist[n];
      KALDI_ASSERT(utt2ivector.count(utt) != 0);
      utts_of_this_spk.Row(n).CopyFromVec(
          *(utt2ivector.find(utt)->second));
    }
    stats.AccStats(utts_of_this_spk);
  }

  // If WLDA was selected, compute between_covar_weighted and within_covar_weighted
  if (lda_variation > 0) {

    KALDI_LOG << "Running WLDA variation: " << lda_variation;

    // WLDA S_w defined as:
    //    S_w = SUM_s=1^S SUM_i=1^n_s (w_i^s − w_s)(w_i^s − w_s)^T
    // where:
    //    S:        total number of speakers
    //    w_s:      mean vector for speaker s
    //    n_s:      num utterances for speaker s
    //    w_i^s:    an utterance for speaker s

    // Calculate within_covar_weighted
    std::map<std::string, std::vector<std::string> >::const_iterator within_iter;
    int32 i = 0;
    for (within_iter = spk2utt.begin(); within_iter != spk2utt.end(); ++within_iter) {
      KALDI_LOG << "Calculating within scatter: " << i++;

      // grab utterances for speaker
      const std::vector<std::string> &uttlist = within_iter->second;
      KALDI_ASSERT(!uttlist.empty());
      int32 num_utt = uttlist.size(); // number of utterances
      // utts_of_spk_i contains utterances of speaker i
      Matrix<double> utts_of_spk(num_utt, dim);
      for (int32 n = 0; n < num_utt; n++) {
        std::string utt = uttlist[n];
        KALDI_ASSERT(utt2ivector.count(utt) != 0);
        utts_of_spk.Row(n).CopyFromVec(
            *(utt2ivector.find(utt)->second));
      }

      stats.AccWeightedStatsWithin(utts_of_spk);
    }

    // WLDA S_b defined as:
    //    S_b = 1/N SUM_i=1^S-1 SUM_j=i+1^S w(d_ij) n_i n_j (w_i − w_j)(w_i − w_j)^T
    // where:
    //    S:        total number of speakers
    //    w(d_ij):  weight calculated by chosen method
    //    n_i/n_j:  number of utterances of speaker i/j
    //    w_i/w_j:  mean ivector of speaker i/j
    //    this computation must take place after the calculation for standard LDA
    //    as certain weight functions (ex. Mahalanobis) require the within class 
    //    covariance to be calculated already.

    // set up outer iterator over the speaker list (for speaker i)
    i = 0;
    std::map<std::string, std::vector<std::string> >::const_iterator outer_iter;
    for (outer_iter = spk2utt.begin(); outer_iter != std::prev(spk2utt.end()); ++outer_iter) {
      KALDI_LOG << "Calculating between scatter: " << i++;

      // grab utterances for speaker i
      const std::vector<std::string> &uttlist_i = outer_iter->second;
      KALDI_ASSERT(!uttlist_i.empty());
      int32 n_i = uttlist_i.size(); // number of utterances (for speaker i).
      // utts_of_spk_i contains utterances of speaker i
      Matrix<double> utts_of_spk_i(n_i, dim);
      for (int32 n = 0; n < n_i; n++) {
        std::string utt = uttlist_i[n];
        KALDI_ASSERT(utt2ivector.count(utt) != 0);
        utts_of_spk_i.Row(n).CopyFromVec(
            *(utt2ivector.find(utt)->second));
      }

      // set up inner iterator over the speaker list (for speaker j)
      std::map<std::string, std::vector<std::string> >::const_iterator inner_iter =  outer_iter;
      ++inner_iter;
      for (; inner_iter != spk2utt.end(); ++inner_iter) {
        
        // grab utterances for speaker j
        const std::vector<std::string> &uttlist_j = inner_iter->second;
        KALDI_ASSERT(!uttlist_j.empty());
        int32 n_j = uttlist_j.size(); // number of utterances (for speaker j).
        // utts_of_spk_j contains utterances of speaker j
        Matrix<double> utts_of_spk_j(n_j, dim);
        for (int32 n = 0; n < n_j; n++) {
          std::string utt = uttlist_j[n];
          KALDI_ASSERT(utt2ivector.count(utt) != 0);
          utts_of_spk_j.Row(n).CopyFromVec(
              *(utt2ivector.find(utt)->second));
        }

        // Call calculation for between_covar_weighted here
        stats.AccWeightedStats(utts_of_spk_i, n_i, utts_of_spk_j, n_j, lda_variation, wlda_n);
      }
    }
  }

  KALDI_LOG << "Stats have " << stats.Info();
  KALDI_ASSERT(!stats.Empty());
  KALDI_ASSERT(!stats.SingularTotCovar() &&
               "Too little data for iVector dimension.");


  SpMatrix<double> total_covar;
  stats.GetTotalCovar(&total_covar);
  SpMatrix<double> within_covar;
  stats.GetWithinCovar(&within_covar);

  // Use within covar weighted if using WLDA
  SpMatrix<double> within_covar_weighted;
  stats.GetWithinCovarWeighted(&within_covar_weighted);


  SpMatrix<double> mat_to_normalize(dim);
  if (lda_variation <= 0) {  // Standard LDA 
    mat_to_normalize.AddSp(total_covariance_factor, total_covar);
    mat_to_normalize.AddSp(1.0 - total_covariance_factor, within_covar);
  } else {  // Weighted LDA
    KALDI_LOG << "Projecting weighted within class covariance";
    mat_to_normalize.AddSp(1.0, within_covar_weighted);
  }

  Matrix<double> T(dim, dim);
  ComputeNormalizingTransform(mat_to_normalize,
    static_cast<double>(covariance_floor), &T);

  SpMatrix<double> between_covar(total_covar);
  between_covar.AddSp(-1.0, within_covar);

  // Use between class covariance weighted if using WLDA
  SpMatrix<double> between_covar_weighted;
  stats.GetBetweenCovarWeighted(&between_covar_weighted);
  
  SpMatrix<double> between_covar_proj(dim);
  if (lda_variation <= 0) {  // Standard LDA 
    between_covar_proj.AddMat2Sp(1.0, T, kNoTrans, between_covar, 0.0);
  } else {  // Weighted LDA
    KALDI_LOG << "Projecting weighted between class covariance";
    between_covar_proj.AddMat2Sp(1.0, T, kNoTrans, between_covar_weighted, 0.0);
  }

  Matrix<double> U(dim, dim);
  Vector<double> s(dim);
  between_covar_proj.Eig(&s, &U);
  bool sort_on_absolute_value = false; // any negative ones will go last (they
                                       // shouldn't exist anyway so doesn't
                                       // really matter)
  SortSvd(&s, &U, static_cast<Matrix<double>*>(NULL),
          sort_on_absolute_value);

  KALDI_LOG << "Singular values of between-class covariance after projecting "
            << "with interpolated [total/within] covariance with a weight of "
            << total_covariance_factor << " on the total covariance, are: " << s;

  // U^T is the transform that will diagonalize the between-class covariance.
  // U_part is just the part of U that corresponds to the kept dimensions.
  SubMatrix<double> U_part(U, 0, dim, 0, lda_dim);

  // We first transform by T and then by U_part^T.  This means T
  // goes on the right.
  Matrix<double> temp(lda_dim, dim);
  temp.AddMatMat(1.0, U_part, kTrans, T, kNoTrans, 0.0);
  lda_out->CopyFromMat(temp);
}

void ComputeAndSubtractMean(
    std::map<std::string, Vector<BaseFloat> *> utt2ivector,
    Vector<BaseFloat> *mean_out) {
  int32 dim = utt2ivector.begin()->second->Dim();
  size_t num_ivectors = utt2ivector.size();
  Vector<double> mean(dim);
  std::map<std::string, Vector<BaseFloat> *>::iterator iter;
  for (iter = utt2ivector.begin(); iter != utt2ivector.end(); ++iter)
    mean.AddVec(1.0 / num_ivectors, *(iter->second));
  mean_out->Resize(dim);
  mean_out->CopyFromVec(mean);
  for (iter = utt2ivector.begin(); iter != utt2ivector.end(); ++iter)
    iter->second->AddVec(-1.0, *mean_out);
}



}

int main(int argc, char *argv[]) {
  using namespace kaldi;
  typedef kaldi::int32 int32;
  try {
    const char *usage =
        "Compute an LDA matrix for iVector system.  Reads in iVectors per utterance,\n"
        "and an utt2spk file which it uses to help work out the within-speaker and\n"
        "between-speaker covariance matrices.  Outputs an LDA projection to a\n"
        "specified dimension.  By default it will normalize so that the projected\n"
        "within-class covariance is unit, but if you set --normalize-total-covariance\n"
        "to true, it will normalize the total covariance.\n"
        "Note: the transform we produce is actually an affine transform which will\n"
        "also set the global mean to zero.\n"
        "\n"
        "Usage:  ivector-compute-lda [options] <ivector-rspecifier> <utt2spk-rspecifier> "
        "<lda-matrix-out>\n"
        "e.g.: \n"
        " ivector-compute-lda ark:ivectors.ark ark:utt2spk lda.mat\n";

    ParseOptions po(usage);

    int32 lda_dim = 100; // Dimension we reduce to
    BaseFloat total_covariance_factor = 0.0,
              covariance_floor = 1.0e-06;
    bool binary = true;

    // Set default behavior to non-weighted, standard LDA
    int32 lda_variation = 0;

    // for weighted LDA
    int32 wlda_n = 4;

    po.Register("dim", &lda_dim, "Dimension we keep with the LDA transform");
    po.Register("total-covariance-factor", &total_covariance_factor,
                "If this is 0.0 we normalize to make the within-class covariance "
                "unit; if 1.0, the total covariance; if between, we normalize "
                "an interpolated matrix.");
    po.Register("covariance-floor", &covariance_floor, "Floor the eigenvalues "
                "of the interpolated covariance matrix to the product of its "
                "largest eigenvalue and this number.");
    po.Register("binary", &binary, "Write output in binary mode");
    po.Register("lda-variation", &lda_variation, 
                "Choose LDA type: \n"
                "   '-1': TEST CASE ONLY - Will create a garbage transform \n"
                "   '0': LDA - no weighting, standard LDA \n"
                "   '1': WLDA - use Euclidean distance weighting function \n"
                "   '2': WLDA - use Mahalanobis distance weighting function \n");
    po.Register("wlda-n", &wlda_n, "Choose n parameter for selected weighting function");

    // check validity of lda variant chosen  
    if (lda_variation > 2) {
      lda_variation = 0;
      KALDI_WARN << "Invalid LDA variant chosen, using standard LDA.";
    } else { // force within-class covariance if weighted LDA is chosen
      if (total_covariance_factor != 0) {
        KALDI_WARN << "total-covariance-factor forced to 0.0 for weighted LDA.";
        total_covariance_factor = 0;
      }
      if (wlda_n == 0) {
        wlda_n = 4;
      }
    }

    po.Read(argc, argv);

    if (po.NumArgs() != 3) {
      po.PrintUsage();
      exit(1);
    }

    std::string ivector_rspecifier = po.GetArg(1),
        utt2spk_rspecifier = po.GetArg(2),
        lda_wxfilename = po.GetArg(3);

    KALDI_ASSERT(covariance_floor >= 0.0);

    int32 num_done = 0, num_err = 0, dim = 0;

    SequentialBaseFloatVectorReader ivector_reader(ivector_rspecifier);
    RandomAccessTokenReader utt2spk_reader(utt2spk_rspecifier);

    std::map<std::string, Vector<BaseFloat> *> utt2ivector;
    std::map<std::string, std::vector<std::string> > spk2utt;

    for (; !ivector_reader.Done(); ivector_reader.Next()) {
      std::string utt = ivector_reader.Key();
      const Vector<BaseFloat> &ivector = ivector_reader.Value();
      if (utt2ivector.count(utt) != 0) {
        KALDI_WARN << "Duplicate iVector found for utterance " << utt
                   << ", ignoring it.";
        num_err++;
        continue;
      }
      if (!utt2spk_reader.HasKey(utt)) {
        KALDI_WARN << "utt2spk has no entry for utterance " << utt
                   << ", skipping it.";
        num_err++;
        continue;
      }
      std::string spk = utt2spk_reader.Value(utt);
      utt2ivector[utt] = new Vector<BaseFloat>(ivector);
      if (dim == 0) {
        dim = ivector.Dim();
      } else {
        KALDI_ASSERT(dim == ivector.Dim() && "iVector dimension mismatch");
      }
      spk2utt[spk].push_back(utt);
      num_done++;
    }

    KALDI_LOG << "Read " << num_done << " utterances, "
              << num_err << " with errors.";

    if (num_done == 0) {
      KALDI_ERR << "Did not read any utterances.";
    } else {
      KALDI_LOG << "Computing within-class covariance.";
    }

    Vector<BaseFloat> mean;
    if (lda_variation <= 0) {
      ComputeAndSubtractMean(utt2ivector, &mean);
    }
    KALDI_LOG << "2-norm of iVector mean is " << mean.Norm(2.0);


    Matrix<BaseFloat> lda_mat(lda_dim, dim + 1); // LDA matrix without the offset term.
    SubMatrix<BaseFloat> linear_part(lda_mat, 0, lda_dim, 0, dim);
    ComputeLdaTransform(utt2ivector,
                        spk2utt,
                        total_covariance_factor,
                        covariance_floor,
                        lda_variation,
                        wlda_n,
                        &linear_part);
    Vector<BaseFloat> offset(lda_dim);
    offset.AddMatVec(-1.0, linear_part, kNoTrans, mean, 0.0);
    lda_mat.CopyColFromVec(offset, dim); // add mean-offset to transform

    KALDI_VLOG(2) << "2-norm of transformed iVector mean is "
                  << offset.Norm(2.0);

    // Test functionality of LDA mat
    if (lda_variation < 0){
      KALDI_LOG << "LDA test case, replacing LDA mat";
      if (wlda_n == 4){
        lda_mat.SetRandUniform();
      } else {
        lda_mat.SetZero();
      }
    } 

    KALDI_LOG << "lda_mat computed as " << lda_mat;

    WriteKaldiObject(lda_mat, lda_wxfilename, binary);

    KALDI_LOG << "Wrote LDA transform to "
              << PrintableWxfilename(lda_wxfilename);

    std::map<std::string, Vector<BaseFloat> *>::iterator iter;
    for (iter = utt2ivector.begin(); iter != utt2ivector.end(); ++iter)
      delete iter->second;
    utt2ivector.clear();

    return 0;
  } catch(const std::exception &e) {
    std::cerr << e.what();
    return -1;
  }
}
