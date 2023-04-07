from pathlib import Path
import pandas as pd
import argparse
from sklearn.model_selection import StratifiedGroupKFold

if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv_path', type=str, required=True)
    parser.add_argument('--target_col', type=str, required=True)
    parser.add_argument('--group_col', type=str, required=True)
    parser.add_argument('--n_splits', type=int, default=5)
    parser.add_argument('--verbose', type=int, default=1)

    parser.add_argument('--out_folder', type=str, default='.')

    args = parser.parse_args()

    csv_path = Path(args.csv_path)
    out_folder = Path(args.out_folder)
    out_folder.mkdir(exist_ok=True, parents=True)

    df = pd.read_csv(csv_path)
    nsplits = args.n_splits
    # Splits
    cv = StratifiedGroupKFold(n_splits = nsplits, shuffle=True, random_state=42)
    train_df = {}
    test_df = {}
    val_df = {}
    for fold, (temp_idx, test_idx) in enumerate(cv.split(df, df[args.target_col], df[args.group_col])):
        test_df[fold] = df.iloc[test_idx]
        temp_df = df.iloc[temp_idx]
        
        train_idx, val_idx = next(iter(cv.split(temp_df, temp_df[args.target_col], temp_df[args.group_col])))
        train_df[fold] = temp_df.iloc[train_idx]
        val_df[fold] = temp_df.iloc[val_idx]

    # Create new columns
    for fold in range(nsplits):
        df.assign(**{str(fold): 0})

    # Assign column values
    for fold in range(nsplits):
        train_list = train_df[fold][args.group_col].unique()
        test_list = test_df[fold][args.group_col].unique()
        val_list = val_df[fold][args.group_col].unique()
        
        df.loc[df[args.group_col].isin(train_list),str(fold)] = 'train'
        df.loc[df[args.group_col].isin(test_list),str(fold)] = 'test'
        df.loc[df[args.group_col].isin(val_list),str(fold)] = 'val'

    df.to_csv(out_folder / (csv_path.stem + f'_{args.n_splits}splits_{args.target_col}.csv'), index=False)
    
    if args.verbose == 1:
        for f in range(nsplits):
            vc = df[df[str(f)] == 'train'][args.target_col].value_counts()
            print(f"#### FOLD {f} ####")
            print("TRAIN")
            print(df[df[str(f)] == 'train'][args.target_col].value_counts())
            print("TEST")
            print(df[df[str(f)] == 'test'][args.target_col].value_counts())
            print("VAL")
            print(df[df[str(f)] == 'val'][args.target_col].value_counts())
            print()
    