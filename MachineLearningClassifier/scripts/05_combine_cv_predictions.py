import pandas as pd
from pathlib import Path
import numpy as np

import argparse

if __name__=='__main__':
    parser = argparse.ArgumentParser()

    parser.add_argument('--model_folder', type=str)
    parser.add_argument('--tag', type=str)
    parser.add_argument('--reference_csv', type=str)
    parser.add_argument('--n_splits', type=int)


    args = parser.parse_args()

    model_folder = Path(args.model_folder)
    out_folder = Path(args.model_folder) / 'predictions'
    out_folder.mkdir(exist_ok=True, parents=True)
    nsplits = args.n_splits

    ref_df = pd.read_csv(args.reference_csv)

    csv_list = []
    idx_list = []
    for fold in range(nsplits):
        # To get the position from the ground truth csv
        idx = ref_df[ref_df[str(fold)] == 'test'].index.values
        
        ffold = "f"+str(fold)
       
        pred_folder = model_folder / ffold # / "predictions" / "{'aug-02'}"
        f = next(pred_folder.glob("**/*.csv"))
        df_fold = pd.read_csv(f)
        csv_list.append(df_fold)
        idx_list.append(idx)

        print(f"fold: {fold} | idx length: {len(idx)} | df length: {len(df_fold)}")
        
    # Rearrange
    df = pd.concat(csv_list, ignore_index=True)
    idx = np.concatenate(idx_list)

    df.index = idx
    df = df.sort_index()

    out_fname = out_folder / f"{model_folder.name}_{args.tag}.csv"
    df.to_csv(out_fname, index=False)
    print(f"Done! Saved output to {out_fname}")