# Machine learning classifer used in the proof of concept of the Riverine Organism Drift Imager
This Resnet-18 based machine learning classifier was developed by Mikko Impiö, based on his Taxonomist classifier (https://github.com/mikkoim/taxonomist), for the application on data collected from the Riverine Organism Drift Imager (RODI). This work was described and published in: "placeholder".

# Installation

This classifier was tested on Windows 10 and 11, compatibility issues might arise using a different operating system.

1. Download or clone this repository
2. Install Anaconda (https://www.anaconda.com/) 
3. Open AnacondaPrompt and change to the classifiers' directory 
4. create a conda environment using the environment.yml file

```bash
conda env create -f environment.yml
conda activate rodi
```

# Preprocessing
1. Ensure that the image dataset is located under data/raw/(e.g. Induced_Organism_Drift_2022)
2. Ensure that the annotations file is located under data/annotations (e.g. Induced_Organism_Drift_2022_annotations_4splits_family.csv)
   Download from: https://www.kaggle.com/datasets/fredericdeschaetzen/induced-organism-drift-2022
3. Ensure that a text-file containing a list of the class names is created and placed under data/classes (e.g. rodi_01_family.txt)
4. Performe train-test-val-splits, in this work, a four-way groupstratified Kfold was performed.

```bash
python scripts/01_train_test_split.py --csv_path "data/annotations/Induced_Organism_Drift_2022_annotations.csv" --target_col "family" --group_col "ind_id" --n_splits 4 --out_folder "data/splits"
```

# Training

For each $fold in {0,1,2,3}, run

```bash
python scripts/03_train.py --data_folder "data/raw/Induced_Organism_Drift_2022/" --dataset_name "rodi" --csv_path "data/splits/Induced_Organism_Drift_2022_annotations_4splits_family.csv" --label "family" --fold $fold --n_classes 7 --class_map "data/classes/rodi_01_family.txt" --imsize 224 --batch_size 128 --aug "aug-02" --load_to_memory "False" --model "resnet18" --opt "adamw" --max_epochs 200 --min_epochs 5 --early_stopping "True" --early_stopping_patience 10 --criterion "cross-entropy" --lr 0.0001 --auto_lr "True" --log_dir "rodi" --out_folder "logs" --out_prefix "rodi" --deterministic "True"
```

Outputs are saved in directory logs/rodi/rodi_resnet18_cross-entropy_b128.

# Predict

For each $fold in {0,1,2,3,4}, using the corresponding $model_weights each fold, run:
```bash
python scripts/04_predict.py --data_folder "data/raw/Induced_Organism_Drift_2022/" --dataset_name "rodi" --csv_path "data/splits/Induced_Organism_Drift_2022_annotations_4splits_family.csv" --label "family" --fold $fold --n_classes 7 --class_map "data/classes/rodi_01_family.txt" --imsize 224 --batch_size 128 --aug "aug-02" --load_to_memory "False" --out_folder "results" --tta "False" --out_prefix "results" --model_weights $model_weights
```
model 

Predictions are saved in model directory, in ```logs/rodi/rodi_resnet18_cross-entropy_b128/$FOLD/predictions```

# Aggegate cross-validation preditions

```bash
python scripts/05_combine_cv_predictions.py --model_folder "logs/rodi/rodi_resnet18_cross-entropy_b128" --tag "aug-02" --reference_csv "data/splits/Induced_Organism_Drift_2022_annotations_4splits_family.csv" --n_splits 4
```

Cross-validation predictions are saved in model directory ```logs/rodi/$MODEL_NAME/predictions```

# Analyze
Analyze the results for example with the ```notebooks/evaluate.ipynb```-notebook. 
Jupyter notebook should have been installed with Anaconda.





