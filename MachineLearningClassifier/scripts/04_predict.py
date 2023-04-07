import argparse
import os

import numpy as np
import pandas as pd
import pytorch_lightning as pl
import benthic_models.benthic_models as ut
import torch
from pathlib import Path, PureWindowsPath

if __name__ == '__main__':
    parser = argparse.ArgumentParser()

    parser = ut.add_dataset_args(parser)
    parser = ut.add_dataloader_args(parser)
    parser = ut.add_model_args(parser)
    parser = ut.add_train_args(parser)
    parser = ut.add_program_args(parser)

    args = parser.parse_args()

    gpu_count = torch.cuda.device_count()

    out_folder = Path(args.model_weights).parents[0] / 'predictions' / str({args.aug})
    out_folder.mkdir(exist_ok=True, parents=True)

    model_stem = Path(args.model_weights).stem

    ckpt = torch.load(
        args.model_weights,
        map_location=torch.device("cuda" if torch.cuda.is_available() else "cpu"),
    )

    if args.class_map:
        class_map = ut.load_class_map(args.class_map)
    else:
        class_map = {"fwd": None, "inv": None}

    dm = ut.LitDataModule(
        data_folder=args.data_folder,
        dataset_name=args.dataset_name,
        csv_path=args.csv_path,
        fold=args.fold,
        label=args.label,
        label_transform=class_map["fwd"],
        imsize=args.imsize,
        batch_size=args.batch_size,
        aug=args.aug,
        load_to_memory=args.load_to_memory,
    )

    model = ut.LitModule(**ckpt["hyper_parameters"])

    model.load_state_dict(ckpt["state_dict"])
    model.label_transform = class_map['inv']
    model.freeze()

    trainer = pl.Trainer(gpus=gpu_count, 
                        fast_dev_run=args.smoke_test,
                        logger=False)

    trainer.test(model, dm)

    dm.visualize_datasets(out_folder)
    
    y_true, y_pred, softmax = model.y_true, model.y_pred, model.softmax
    df_pred = pd.DataFrame({"y_true": y_true, "y_pred": y_pred})

    n_classes = softmax.shape[1]
    classes = class_map['inv'](list(range(n_classes)))
    df_prob = pd.DataFrame(data=softmax, columns=classes)
    
    df = pd.concat((df_pred,df_prob),axis=1)

    if not args.smoke_test:
        outname = f"{args.out_prefix}_{model_stem}_{args.aug}.csv"
        df.to_csv(out_folder / outname, index=False)

        if args.tta:
            trainer.test(model, dataloaders=dm.tta_dataloader())
            y_true_tta = dm.tta_process(model.y_true)
            y_pred_tta = dm.tta_process(model.y_pred)
            df_tta = pd.DataFrame({"y_true": y_true_tta, "y_pred": y_pred_tta})

            outname_tta = f"{args.out_prefix}_tta_{model_stem}_{args.aug}.csv"
            df_tta.to_csv(out_folder / outname_tta, index=False)
