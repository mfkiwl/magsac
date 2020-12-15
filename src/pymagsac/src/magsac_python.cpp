#include "magsac_python.hpp"
#include "magsac.h"
#include "fundamental_estimator.h"
#include "homography_estimator.h"
#include "types.h"
#include "model.h"
#include "utils.h"
#include "estimators.h"
#include "most_similar_inlier_selector.h"
#include <thread>

int findFundamentalMatrix_(std::vector<double>& srcPts,
    std::vector<double>& dstPts,
    std::vector<bool>& inliers,
    std::vector<double>& F,
    bool use_magsac_plus_plus,
    double sigma_max,
    double conf,
    int max_iters,
    int partition_num)
{
    magsac::utils::DefaultFundamentalMatrixEstimator estimator(0.1); // The robust homography estimator class containing the
    gcransac::FundamentalMatrix model; // The estimated model

    MAGSAC<cv::Mat, magsac::utils::DefaultFundamentalMatrixEstimator>* magsac;
    if (use_magsac_plus_plus)
        magsac = new MAGSAC<cv::Mat, magsac::utils::DefaultFundamentalMatrixEstimator>(
            MAGSAC<cv::Mat, magsac::utils::DefaultFundamentalMatrixEstimator>::MAGSAC_PLUS_PLUS);
    else
        magsac = new MAGSAC<cv::Mat, magsac::utils::DefaultFundamentalMatrixEstimator>(
            MAGSAC<cv::Mat, magsac::utils::DefaultFundamentalMatrixEstimator>::MAGSAC_ORIGINAL);
    magsac->setMaximumThreshold(sigma_max); // The maximum noise scale sigma allowed
    magsac->setCoreNumber(1); // The number of cores used to speed up sigma-consensus
    magsac->setPartitionNumber(partition_num); // The number partitions used for speeding up sigma consensus. As the value grows, the algorithm become slower and, usually, more accurate.
    magsac->setIterationLimit(max_iters);

    int num_tents = srcPts.size() / 2;
    cv::Mat points(num_tents, 4, CV_64F);
    for (int i = 0; i < num_tents; ++i) {
        points.at<double>(i, 0) = srcPts[2 * i];
        points.at<double>(i, 1) = srcPts[2 * i + 1];
        points.at<double>(i, 2) = dstPts[2 * i];
        points.at<double>(i, 3) = dstPts[2 * i + 1];
    }
    gcransac::sampler::UniformSampler main_sampler(&points);

    ModelScore score;
    bool success = magsac->run(points, // The data points
        conf, // The required confidence in the results
        estimator, // The used estimator
        main_sampler, // The sampler used for selecting minimal samples in each iteration
        model, // The estimated model
        max_iters, // The number of iterations
        score); // The score of the estimated model
    inliers.resize(num_tents);
    if (!success) {
        for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx) {
            inliers[pt_idx] = false;
        }
        F.resize(9);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                F[i * 3 + j] = 0;
            }
        }
        return 0;
    }
    int num_inliers = 0;
    for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx) {
        const int is_inlier = estimator.residual(points.row(pt_idx), model.descriptor) <= sigma_max;
        inliers[pt_idx] = (bool)is_inlier;
        num_inliers += is_inlier;
    }

    F.resize(9);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            F[i * 3 + j] = (double)model.descriptor(i, j);
        }
    }
    return num_inliers;
}

int findEssentialMatrix_(std::vector<double>& srcPts,
    std::vector<double>& dstPts,
    std::vector<bool>& inliers,
    std::vector<double>& E,
    std::vector<double>& src_K,
    std::vector<double>& dst_K,
    bool use_magsac_plus_plus,
    double sigma_max,
    double conf,
    int max_iters,
    int partition_num)
{
    int num_tents = srcPts.size() / 2;
    cv::Mat points(num_tents, 4, CV_64F);
    for (int i = 0; i < num_tents; ++i) {
        points.at<double>(i, 0) = srcPts[2 * i];
        points.at<double>(i, 1) = srcPts[2 * i + 1];
        points.at<double>(i, 2) = dstPts[2 * i];
        points.at<double>(i, 3) = dstPts[2 * i + 1];
    }

    Eigen::Matrix3d intrinsics_src,
        intrinsics_dst;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            intrinsics_src(i, j) = src_K[i * 3 + j];
        }
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            intrinsics_dst(i, j) = dst_K[i * 3 + j];
        }
    }

    const double &fx1 = intrinsics_src(0, 0);
    const double &fy1 = intrinsics_src(1, 1);
    const double &fx2 = intrinsics_dst(0, 0);
    const double &fy2 = intrinsics_dst(1, 1);

    const double threshold_normalizer =
        (fx1 + fx2 + fy1 + fy2) / 4.0;
    const double normalized_sigma_max =
        sigma_max / threshold_normalizer;

    cv::Mat normalized_points(points.size(), CV_64F);
    gcransac::utils::normalizeCorrespondences(points,
        intrinsics_src,
        intrinsics_dst,
        normalized_points);

    magsac::utils::DefaultEssentialMatrixEstimator estimator(
        intrinsics_src,
        intrinsics_dst); // The robust essential matrix estimator class
    gcransac::EssentialMatrix model; // The estimated model

    MAGSAC<cv::Mat, magsac::utils::DefaultEssentialMatrixEstimator> magsac(
        use_magsac_plus_plus ?
            MAGSAC<cv::Mat, magsac::utils::DefaultEssentialMatrixEstimator>::MAGSAC_PLUS_PLUS : 
            MAGSAC<cv::Mat, magsac::utils::DefaultEssentialMatrixEstimator>::MAGSAC_ORIGINAL);

    magsac.setMaximumThreshold(normalized_sigma_max); // The maximum noise scale sigma allowed
    magsac.setCoreNumber(1); // The number of cores used to speed up sigma-consensus
    magsac.setPartitionNumber(partition_num); // The number partitions used for speeding up sigma consensus. As the value grows, the algorithm become slower and, usually, more accurate.
    magsac.setIterationLimit(max_iters);
    magsac.setReferenceThreshold(magsac.getReferenceThreshold() / threshold_normalizer); // The reference threshold inside MAGSAC++ should also be normalized.

    gcransac::sampler::UniformSampler main_sampler(&points);

    ModelScore score;
    bool success = magsac.run(normalized_points, // The data points
        conf, // The required confidence in the results
        estimator, // The used estimator
        main_sampler, // The sampler used for selecting minimal samples in each iteration
        model, // The estimated model
        max_iters, // The number of iterations
        score); // The score of the estimated model
    inliers.resize(num_tents);

    if (!success) {
        for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx) {
            inliers[pt_idx] = false;
        }
        E.resize(9);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                E[i * 3 + j] = 0;
            }
        }
        return 0;
    }

    int num_inliers = 0;
    for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx) {
        const int is_inlier = 
            estimator.residual(normalized_points.row(pt_idx), model.descriptor) <= normalized_sigma_max;
        inliers[pt_idx] = (bool)is_inlier;
        num_inliers += is_inlier;
    }

    E.resize(9);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            E[i * 3 + j] = (double)model.descriptor(i, j);
        }
    }

    return num_inliers;
}

int findHomography_(std::vector<double>& srcPts,
                    std::vector<double>& dstPts,
                    std::vector<bool>& inliers,
                    std::vector<double>& H,
					bool use_magsac_plus_plus,
                    double sigma_max,
                    double conf,
                    int max_iters,
                    int partition_num)

{
    magsac::utils::DefaultHomographyEstimator estimator; // The robust homography estimator class containing the function for the fitting and residual calculation
    gcransac::Homography model; // The estimated model

    MAGSAC<cv::Mat, magsac::utils::DefaultHomographyEstimator>* magsac;
    if (use_magsac_plus_plus)
        magsac = new MAGSAC<cv::Mat, magsac::utils::DefaultHomographyEstimator>(
            MAGSAC<cv::Mat, magsac::utils::DefaultHomographyEstimator>::MAGSAC_PLUS_PLUS);
    else
        magsac = new MAGSAC<cv::Mat, magsac::utils::DefaultHomographyEstimator>(
            MAGSAC<cv::Mat, magsac::utils::DefaultHomographyEstimator>::MAGSAC_ORIGINAL);

    magsac->setMaximumThreshold(sigma_max); // The maximum noise scale sigma allowed
    magsac->setCoreNumber(1); // The number of cores used to speed up sigma-consensus
    magsac->setPartitionNumber(partition_num); // The number partitions used for speeding up sigma consensus. As the value grows, the algorithm become slower and, usually, more accurate.
    magsac->setIterationLimit(max_iters);
    
	ModelScore score;
	
    int num_tents = srcPts.size()/2;
    cv::Mat points(num_tents, 4, CV_64F);
    for (int i = 0; i < num_tents; ++i) {
        points.at<double>(i, 0) = srcPts[2*i];
        points.at<double>(i, 1) = srcPts[2*i + 1];
        points.at<double>(i, 2) = dstPts[2*i];
        points.at<double>(i, 3) = dstPts[2*i + 1];
    }
    gcransac::sampler::UniformSampler main_sampler(&points);

    bool success = magsac->run(points, // The data points
                              conf, // The required confidence in the results
                              estimator, // The used estimator
                              main_sampler, // The sampler used for selecting minimal samples in each iteration
                              model, // The estimated model
                              max_iters, // The number of iterations
							  score); // The score of the estimated model
    inliers.resize(num_tents);
    if (!success) {
        for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx) {
            inliers[pt_idx] = false;
        }
        H.resize(9);
        for (int i = 0; i < 3; i++){
            for (int j = 0; j < 3; j++){
                H[i*3+j] = 0;
            }
        }
        return 0;
    }

    int num_inliers = 0;
    for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx) {
        const int is_inlier = sqrt(estimator.residual(points.row(pt_idx), model.descriptor)) <= sigma_max;
        inliers[pt_idx] = (bool)is_inlier;
        num_inliers+=is_inlier;
    }
    
    H.resize(9);
    
    for (int i = 0; i < 3; i++){
        for (int j = 0; j < 3; j++){
            H[i*3+j] = (double)model.descriptor(i,j);
        }
    }
    return num_inliers;
}

int adaptiveInlierSelection_(
    const std::vector<double>& srcPts_,
    const std::vector<double>& dstPts_,
    const std::vector<double>& model_,
    std::vector<bool>& inliers_,
    double &bestThreshold_,
    int problemType_,
    double maximumThreshold_,
    int minimumInlierNumber_)
{
    if (problemType_ > 2)
    {
        printf("The valid settings for variable 'problemType' are\n\t 0 (homography)\n\t 1 (fundamental matrix) \n\t 2 (essential matrix)\n");
        return 0;
    }

    int num_tents = srcPts_.size() / 2;
    cv::Mat points(num_tents, 4, CV_64F);
    for (int i = 0; i < num_tents; ++i) {
        points.at<double>(i, 0) = srcPts_[2 * i];
        points.at<double>(i, 1) = srcPts_[2 * i + 1];
        points.at<double>(i, 2) = dstPts_[2 * i];
        points.at<double>(i, 3) = dstPts_[2 * i + 1];
    }

    std::vector<size_t> selectedInliers;
    double bestThreshold;

    if (problemType_ == 0)
    {
        gcransac::Model model;
        model.descriptor.resize(3, 3);
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 3; ++c)
                model.descriptor(r, c) = model_[3 * r + c];

        MostSimilarInlierSelector<magsac::utils::DefaultHomographyEstimator>
            inlierSelector(
                MAX(magsac::utils::DefaultHomographyEstimator::sampleSize() + 1, minimumInlierNumber_),
                maximumThreshold_);

        // The robust homography estimator class containing the function for the fitting and residual calculation
        magsac::utils::DefaultHomographyEstimator homographyEstimator;

        inlierSelector.selectInliers(points,
            homographyEstimator,
            model,
            selectedInliers,
            bestThreshold_);
    }
    else if (problemType_ == 1)
    {
        gcransac::Model model;
        model.descriptor.resize(3, 3);
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 3; ++c)
                model.descriptor(r, c) = model_[3 * r + c];

        MostSimilarInlierSelector<magsac::utils::DefaultFundamentalMatrixEstimator>
            inlierSelector(
                MAX(magsac::utils::DefaultFundamentalMatrixEstimator::sampleSize() + 1, minimumInlierNumber_),
                maximumThreshold_);

        // The robust fundamental matrix estimator class containing the function for the fitting and residual calculation
        magsac::utils::DefaultFundamentalMatrixEstimator fundamentalEstimator(maximumThreshold_);

        inlierSelector.selectInliers(points,
            fundamentalEstimator,
            model,
            selectedInliers,
            bestThreshold_);
    }
    else
    {
        printf("Note: for essential matrices, the correspondences should be normalized by the intrinsic camera matrices.\n");

        gcransac::Model model;
        model.descriptor.resize(3, 3);
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 3; ++c)
                model.descriptor(r, c) = model_[3 * r + c];

        MostSimilarInlierSelector<magsac::utils::DefaultEssentialMatrixEstimator>
            inlierSelector(
                MAX(magsac::utils::DefaultEssentialMatrixEstimator::sampleSize() + 1, minimumInlierNumber_),
                maximumThreshold_);

        // The robust essential matrix estimator class containing the function for the fitting and residual calculation
        magsac::utils::DefaultEssentialMatrixEstimator essentialEstimator(
            Eigen::Matrix3d::Identity(),
            Eigen::Matrix3d::Identity());

        inlierSelector.selectInliers(points,
            essentialEstimator,
            model,
            selectedInliers,
            bestThreshold_);
    }

    inliers_.resize(num_tents);

    for (auto pt_idx = 0; pt_idx < points.rows; ++pt_idx)
        inliers_[pt_idx] = false;
   
    for (const auto& inlierIdx : selectedInliers)
        inliers_[inlierIdx] = true;

    return selectedInliers.size();
}